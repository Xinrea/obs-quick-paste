#include "image-storage.hpp"
#include "obs-source-factory.hpp"
#include "paste-event-filter.hpp"
#include "plugin-settings.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/bmem.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPromise>
#include <QPushButton>
#include <QRunnable>
#include <QSet>
#include <QSpinBox>
#include <QStatusBar>
#include <QThreadPool>
#include <QVBoxLayout>
#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-quick-paste", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
    return "Paste clipboard images as persistent native OBS image sources";
}

namespace quickpaste {
namespace {
QString text(const char *key)
{
    return QString::fromUtf8(obs_module_text(key));
}

class PluginLifecycle final : public QObject {
public:
    explicit PluginLifecycle(QWidget *mainWindow, QString configDirectory)
        : mainWindow_(mainWindow), configDirectory_(std::move(configDirectory))
    {
        QString error;
        settings_ = PluginSettings::load(configDirectory_, &error);
        pool_.setMaxThreadCount(1);
        pool_.setExpiryTimeout(-1);
        filter_ = new PasteEventFilter(mainWindow, {
            [this] { return !stopping_ && copyHookAvailable_ && settings_.enabled; },
            [this] { return settings_.limits; },
            [this](QImage image) { paste(std::move(image)); },
            [this](QString message) { report(message); },
        }, this);

        // OBS has no public source-clipboard query. Observe the existing Qt
        // action, without accessing private OBS classes or intercepting Copy.
        auto *copy = mainWindow->findChild<QAction *>(QStringLiteral("actionCopySource"));
        copyHookAvailable_ = copy != nullptr;
        if (copy) {
            connect(copy, &QAction::triggered, filter_, &PasteEventFilter::nativeSourceCopied);
            connect(copy, &QObject::destroyed, this, [this] {
                copyHookAvailable_ = false;
                filter_->reset();
            });
        } else {
            report(text("CopyHookUnavailable"));
        }
        qApp->installEventFilter(filter_);
        toolsAction_ = static_cast<QAction *>(
            obs_frontend_add_tools_menu_qaction(obs_module_text("Settings.Title")));
        if (toolsAction_)
            connect(toolsAction_, &QAction::triggered, this, [this] { showSettings(); });
        if (!error.isEmpty())
            report(error);
    }

    ~PluginLifecycle() override
    {
        stopping_ = true;
        if (qApp)
            qApp->removeEventFilter(filter_);
        if (toolsAction_) {
            toolsAction_->disconnect(this);
            toolsAction_->setEnabled(false);
        }
        if (dialog_)
            delete dialog_;
        // Workers only encode/write QImages; they never touch OBS or UI.
        // Drain them before unloading plugin code and remove only files from
        // jobs which never reached source creation.
        pool_.waitForDone();
        for (auto *watcher : pending_) {
            watcher->disconnect(this);
            const auto result = watcher->result();
            removeUnreferencedFile(result.path);
        }
    }

    void invalidate()
    {
        ++generation_;
        filter_->reset();
    }

    void collectionCleared()
    {
        invalidate();
        // OBS clears its private source clipboard before emitting CLEANUP.
        filter_->nativeSourceClipboardCleared();
    }

private:
    void report(const QString &message)
    {
        blog(LOG_WARNING, "[obs-quick-paste] %s", message.toUtf8().constData());
        if (auto *window = qobject_cast<QMainWindow *>(mainWindow_.data()))
            window->statusBar()->showMessage(text("ErrorPrefix") + message, 15000);
    }

    void removeUnreferencedFile(const QString &path)
    {
        if (!path.isEmpty() && !QFile::remove(path))
            blog(LOG_WARNING, "[obs-quick-paste] Could not remove unreferenced file: %s",
                 path.toUtf8().constData());
    }

    void paste(QImage image)
    {
        constexpr qint64 maxQueuedBytes = 256LL * 1024 * 1024;
        if (pending_.size() >= 4 || image.sizeInBytes() > maxQueuedBytes - queuedBytes_) {
            report(text("QueueFull"));
            return;
        }
        OBSSourceAutoRelease target = obs_frontend_preview_program_mode_active()
            ? obs_frontend_get_current_preview_scene() : obs_frontend_get_current_scene();
        if (!target || !obs_scene_from_source(target)) {
            report(text("NoScene"));
            return;
        }
        const QString sceneUuid = QString::fromUtf8(obs_source_get_uuid(target));
        const auto generation = generation_;
        const QSize size = image.size();
        const qint64 bytes = image.sizeInBytes();
        const SourceOptions options{settings_.autoCenter, settings_.maxCanvasRatio,
                                    settings_.autoSelect, settings_.bringToFront};
        auto *watcher = new QFutureWatcher<StorageResult>(this);
        pending_.insert(watcher);
        queuedBytes_ += bytes;
        connect(watcher, &QFutureWatcher<StorageResult>::finished, this,
                [this, watcher, generation, sceneUuid, size, bytes, options] {
            pending_.remove(watcher);
            queuedBytes_ -= bytes;
            const auto result = watcher->result();
            watcher->deleteLater();
            if (!result.error.isEmpty()) {
                report(result.error);
                return;
            }
            if (stopping_ || generation != generation_ || !settings_.enabled) {
                removeUnreferencedFile(result.path);
                if (!stopping_)
                    report(text("PasteCancelled"));
                return;
            }
            const QString error = OBSSourceFactory::create(result.path, size, sceneUuid, options);
            if (!error.isEmpty()) {
                removeUnreferencedFile(result.path);
                report(error);
            }
        });
        const QString directory = settings_.storageDirectory;
        auto promise = std::make_shared<QPromise<StorageResult>>();
        promise->start();
        watcher->setFuture(promise->future());
        pool_.start(QRunnable::create([promise, image = std::move(image), directory] {
            try {
                promise->addResult(ImageStorage::save(image, directory));
            } catch (...) {
                promise->addResult(StorageResult{{}, QStringLiteral("Unexpected image storage failure.")});
            }
            promise->finish();
        }));
    }

    void showSettings()
    {
        if (dialog_) {
            dialog_->raise();
            dialog_->activateWindow();
            return;
        }
        auto *dialog = new QDialog(mainWindow_);
        dialog_ = dialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(text("Settings.Title"));
        auto *layout = new QVBoxLayout(dialog);
        auto *form = new QFormLayout;
        layout->addLayout(form);
        auto check = [form, dialog](const char *key, bool value) {
            auto *widget = new QCheckBox(text(key), dialog);
            widget->setChecked(value);
            form->addRow(widget);
            return widget;
        };
        auto *enabled = check("Settings.Enabled", settings_.enabled);
        auto *center = check("Settings.AutoCenter", settings_.autoCenter);
        auto *select = check("Settings.AutoSelect", settings_.autoSelect);
        auto *front = check("Settings.BringToFront", settings_.bringToFront);
        auto *ratio = new QDoubleSpinBox(dialog);
        ratio->setRange(1, 100);
        ratio->setSuffix(QStringLiteral("%"));
        ratio->setValue(settings_.maxCanvasRatio * 100);
        form->addRow(text("Settings.MaxRatio"), ratio);
        auto *directory = new QLineEdit(settings_.storageDirectory, dialog);
        auto *browse = new QPushButton(text("Settings.Browse"), dialog);
        auto *pathRow = new QHBoxLayout;
        pathRow->addWidget(directory);
        pathRow->addWidget(browse);
        form->addRow(text("Settings.Directory"), pathRow);
        connect(browse, &QPushButton::clicked, dialog, [dialog, directory] {
            const auto path = QFileDialog::getExistingDirectory(dialog, text("Settings.Directory"),
                                                                directory->text());
            if (!path.isEmpty())
                directory->setText(path);
        });
        auto *dimension = new QSpinBox(dialog);
        dimension->setRange(1, 32768);
        dimension->setValue(settings_.limits.maxDimension);
        form->addRow(text("Settings.MaxDimension"), dimension);
        auto *pixels = new QSpinBox(dialog);
        pixels->setRange(1, 268435456);
        pixels->setValue(static_cast<int>(settings_.limits.maxPixels));
        form->addRow(text("Settings.MaxPixels"), pixels);
        auto *cleanup = check("Settings.Cleanup", false);
        cleanup->setEnabled(false);
        auto *note = new QLabel(text("Settings.RetentionNote"), dialog);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                            dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
            PluginSettings updated = settings_;
            updated.enabled = enabled->isChecked();
            updated.autoCenter = center->isChecked();
            updated.autoSelect = select->isChecked();
            updated.bringToFront = front->isChecked();
            updated.maxCanvasRatio = ratio->value() / 100;
            updated.storageDirectory = QDir::cleanPath(directory->text());
            updated.limits.maxDimension = dimension->value();
            updated.limits.maxPixels = pixels->value();
            QString error;
            if (!updated.save(configDirectory_, &error)) {
                QMessageBox::warning(dialog, text("Settings.Title"), error);
                return;
            }
            settings_ = updated;
            invalidate();
            dialog->accept();
        });
        dialog->show();
    }

    QPointer<QWidget> mainWindow_;
    QString configDirectory_;
    PluginSettings settings_;
    PasteEventFilter *filter_ = nullptr;
    QPointer<QAction> toolsAction_;
    QPointer<QDialog> dialog_;
    QThreadPool pool_;
    QSet<QFutureWatcher<StorageResult> *> pending_;
    qint64 queuedBytes_ = 0;
    quint64 generation_ = 0;
    bool copyHookAvailable_ = false;
    bool stopping_ = false;
};

std::unique_ptr<PluginLifecycle> plugin;

void frontendEvent(enum obs_frontend_event event, void *)
{
    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING: {
        if (plugin || !qApp)
            break;
        auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
        char *path = obs_module_config_path("");
        if (mainWindow && path)
            plugin = std::make_unique<PluginLifecycle>(mainWindow, QString::fromUtf8(path));
        else
            blog(LOG_ERROR, "[obs-quick-paste] Main window or plugin configuration path unavailable");
        bfree(path);
        break;
    }
    case OBS_FRONTEND_EVENT_SCENE_CHANGED:
    case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
    case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
    case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
        if (plugin)
            plugin->invalidate();
        break;
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
        if (plugin)
            plugin->collectionCleared();
        break;
    case OBS_FRONTEND_EVENT_EXIT:
    case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
        plugin.reset();
        break;
    default:
        break;
    }
}
} // namespace
} // namespace quickpaste

bool obs_module_load(void)
{
    obs_frontend_add_event_callback(quickpaste::frontendEvent, nullptr);
    blog(LOG_INFO, "[obs-quick-paste] Loaded; waiting for frontend initialization");
    return true;
}

void obs_module_unload(void)
{
    obs_frontend_remove_event_callback(quickpaste::frontendEvent, nullptr);
    quickpaste::plugin.reset();
    quickpaste::OBSSourceFactory::shutdown();
}
