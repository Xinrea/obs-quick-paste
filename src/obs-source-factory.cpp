#include "obs-source-factory.hpp"

#include "source-layout.hpp"

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QCoreApplication>
#include <QFileInfo>
#include <QThread>

#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace quickpaste {
namespace {

// Module-lifetime state only: no callbacks capture the plugin/controller.
// OBS normally unloads native modules at shutdown. Hot-unloading this module
// while OBS retains undo function pointers is not supported by the frontend API.
long long collectionEpoch = 1;
bool collectionGuardInstalled = false;

bool onUiThread()
{
	auto *application = QCoreApplication::instance();
	return application && QThread::currentThread() == application->thread();
}

void collectionEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING ||
	    event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP || event == OBS_FRONTEND_EVENT_EXIT)
		++collectionEpoch;
}

QString collectionName()
{
	char *name = obs_frontend_get_current_scene_collection();
	const QString result = QString::fromUtf8(name ? name : "");
	bfree(name);
	return result;
}

bool currentTarget(obs_source_t *target)
{
	OBSSourceAutoRelease current = obs_frontend_preview_program_mode_active()
					      ? obs_frontend_get_current_preview_scene()
					      : obs_frontend_get_current_scene();
	return target && current == target && !obs_source_removed(target);
}

bool sameUuid(obs_source_t *source, const char *uuid)
{
	return source && uuid && *uuid && std::strcmp(obs_source_get_uuid(source), uuid) == 0;
}

QString uniqueName(const QString &base)
{
	// Unlike obs_get_source_by_name, this includes scenes on other canvases.
	for (qulonglong suffix = 1;; ++suffix) {
		const QString candidate = suffix == 1 ? base : QStringLiteral("%1 (%2)").arg(base).arg(suffix);
		struct Search {
			QByteArray name;
			bool found = false;
		} search{candidate.toUtf8()};
		obs_enum_all_sources(
			[](void *data, obs_source_t *source) {
				auto &search = *static_cast<Search *>(data);
				search.found = search.name == obs_source_get_name(source);
				return !search.found;
			},
			&search);
		if (!search.found)
			return candidate;
	}
}

using Selection = std::vector<std::pair<OBSSceneItem, bool>>;

bool rememberSelection(obs_scene_t *, obs_sceneitem_t *item, void *data)
{
	auto &selection = *static_cast<Selection *>(data);
	selection.emplace_back(OBSSceneItem(item), obs_sceneitem_selected(item));
	if (obs_sceneitem_is_group(item))
		obs_sceneitem_group_enum_items(item, rememberSelection, data);
	return true;
}

void applySelection(obs_sceneitem_t *added, const Selection &previous, bool autoSelect)
{
	// OBS 32.0.4 OBSBasic::AddSceneItem automatically selects the added item.
	// On the UI thread its item_add handler runs synchronously. Restore the
	// snapshot when autoSelect is off, including children of collapsed groups.
	// Public obs_sceneitem_select emits item_select/item_deselect; SourceTreeItem
	// forwards those to SourceTree::SelectItem. No private UI classes are needed.
	// This synchronizes selection/highlighting, not keyboard focus or scrolling.
	for (const auto &entry : previous)
		obs_sceneitem_select(entry.first, autoSelect ? false : entry.second);
	obs_sceneitem_select(added, autoSelect);
}

void saveTransform(obs_data_t *data, obs_sceneitem_t *item)
{
	obs_transform_info transform{};
	obs_sceneitem_get_info2(item, &transform);
	obs_data_set_double(data, "x", transform.pos.x);
	obs_data_set_double(data, "y", transform.pos.y);
	obs_data_set_double(data, "scale_x", transform.scale.x);
	obs_data_set_double(data, "scale_y", transform.scale.y);
	obs_data_set_double(data, "rotation", transform.rot);
	obs_data_set_int(data, "alignment", transform.alignment);
	obs_data_set_int(data, "bounds_type", transform.bounds_type);
	obs_data_set_int(data, "bounds_alignment", transform.bounds_alignment);
	obs_data_set_double(data, "bounds_x", transform.bounds.x);
	obs_data_set_double(data, "bounds_y", transform.bounds.y);
	obs_data_set_bool(data, "crop_to_bounds", transform.crop_to_bounds);
	obs_data_set_int(data, "order", obs_sceneitem_get_order_position(item));
}

void restoreTransform(obs_data_t *data, obs_sceneitem_t *item)
{
	obs_transform_info transform{};
	vec2_set(&transform.pos, float(obs_data_get_double(data, "x")), float(obs_data_get_double(data, "y")));
	vec2_set(&transform.scale, float(obs_data_get_double(data, "scale_x")),
		 float(obs_data_get_double(data, "scale_y")));
	transform.rot = float(obs_data_get_double(data, "rotation"));
	transform.alignment = uint32_t(obs_data_get_int(data, "alignment"));
	transform.bounds_type = obs_bounds_type(obs_data_get_int(data, "bounds_type"));
	transform.bounds_alignment = uint32_t(obs_data_get_int(data, "bounds_alignment"));
	vec2_set(&transform.bounds, float(obs_data_get_double(data, "bounds_x")),
		 float(obs_data_get_double(data, "bounds_y")));
	transform.crop_to_bounds = obs_data_get_bool(data, "crop_to_bounds");
	obs_sceneitem_set_info2(item, &transform);
	obs_sceneitem_set_order_position(item, int(obs_data_get_int(data, "order")));
}

bool validAction(obs_data_t *data)
{
	return onUiThread() && data && obs_data_get_int(data, "collection_epoch") == collectionEpoch &&
	       collectionName() == QString::fromUtf8(obs_data_get_string(data, "collection"));
}

void undoCreate(const char *json)
{
	OBSDataAutoRelease data = obs_data_create_from_json(json);
	if (!validAction(data))
		return;
	OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(obs_data_get_string(data, "scene_uuid"));
	if (!sceneSource || obs_source_removed(sceneSource))
		return;
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, obs_data_get_int(data, "item_id"));
	if (!item || !sameUuid(obs_sceneitem_get_source(item), obs_data_get_string(data, "source_uuid")))
		return;

	// Only remove our item. libobs releases its source reference; if another
	// item uses the source it must remain intact. The source registry is weak,
	// so the last reference going away destroys an otherwise unused source.
	// Never remove the persisted file: redo and other items may still need it.
	obs_sceneitem_remove(item);
	obs_frontend_save();
}

void redoCreate(const char *json)
{
	OBSDataAutoRelease data = obs_data_create_from_json(json);
	if (!validAction(data))
		return;
	OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(obs_data_get_string(data, "scene_uuid"));
	if (!sceneSource || obs_source_removed(sceneSource))
		return;
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene)
		return;
	const auto itemId = obs_data_get_int(data, "item_id");
	// A repeated callback or reused ID must not duplicate or overwrite an item.
	if (obs_scene_find_sceneitem_by_id(scene, itemId))
		return;

	const char *uuid = obs_data_get_string(data, "source_uuid");
	OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid);
	if (!source) {
		OBSDataAutoRelease saved = obs_data_get_obj(data, "source");
		if (!saved || std::strcmp(obs_data_get_string(saved, "uuid"), uuid) != 0)
			return;
		const QByteArray name = uniqueName(QString::fromUtf8(obs_data_get_string(saved, "name"))).toUtf8();
		obs_data_set_string(saved, "name", name.constData());
		// obs_load_source restores the saved UUID; creating a fresh image_source
		// would break later undo actions that refer to this source by UUID.
		source = obs_load_source(saved);
	}
	if (!source || obs_source_removed(source) || obs_obj_invalid(source) || !sameUuid(source, uuid))
		return;

	Selection previous;
	obs_scene_enum_items(scene, rememberSelection, &previous);
	OBSSceneItem item = obs_scene_add(scene, source);
	if (!item)
		return; // RAII also releases a newly loaded, otherwise unreferenced source.
	obs_sceneitem_set_id(item, itemId);
	restoreTransform(data, item);
	applySelection(item, previous, obs_data_get_bool(data, "auto_select"));
	obs_frontend_save();
}

} // namespace

void OBSSourceFactory::shutdown()
{
	++collectionEpoch;
	if (collectionGuardInstalled) {
		obs_frontend_remove_event_callback(collectionEvent, nullptr);
		collectionGuardInstalled = false;
	}
}

QString OBSSourceFactory::create(const QString &absoluteImagePath, const QSize &imageSize, const QString &targetSceneUuid,
				 const SourceOptions &options)
{
	if (!onUiThread())
		return QStringLiteral("Image sources must be created on the OBS UI thread.");
	if (!collectionGuardInstalled) {
		obs_frontend_add_event_callback(collectionEvent, nullptr);
		collectionGuardInstalled = true;
	}
	const QString collection = collectionName();
	const auto epoch = collectionEpoch;
	if (collection.isEmpty())
		return QStringLiteral("The current scene collection could not be resolved.");
	if (targetSceneUuid.isEmpty())
		return QStringLiteral("No target scene was captured.");
	OBSSourceAutoRelease sceneSource = obs_get_source_by_uuid(targetSceneUuid.toUtf8().constData());
	if (!sceneSource || !currentTarget(sceneSource))
		return QStringLiteral("The target scene changed or no longer exists; paste was cancelled.");
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene)
		return QStringLiteral("The captured target is not an OBS scene.");

	// OBS 32 native canvas API, not the global/main video resolution. Never
	// substitute another canvas if this scene's native canvas cannot be read.
	OBSCanvasAutoRelease canvas = obs_source_get_canvas(sceneSource);
	obs_video_info video{};
	if (!canvas || obs_canvas_removed(canvas) || !obs_canvas_get_video_info(canvas, &video) ||
	    video.base_width > uint32_t(std::numeric_limits<int>::max()) ||
	    video.base_height > uint32_t(std::numeric_limits<int>::max()))
		return QStringLiteral("The target scene's native canvas resolution is unavailable.");
	const auto layout = SourceLayout::calculate(imageSize, QSize(int(video.base_width), int(video.base_height)),
						    options.autoCenter, options.maxCanvasRatio);
	if (!layout)
		return QStringLiteral("The image size, canvas size, or maximum canvas ratio is invalid.");
	const QFileInfo file(absoluteImagePath);
	if (!file.isAbsolute() || !file.isFile() || !file.isReadable())
		return QStringLiteral("The persisted image is not a readable absolute file path.");
	if (!obs_source_get_display_name("image_source"))
		return QStringLiteral("The OBS image source module is unavailable.");

	const QByteArray name = uniqueName(QStringLiteral("Quick Paste")).toUtf8();
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "file", absoluteImagePath.toUtf8().constData());
	OBSSourceAutoRelease source = obs_source_create("image_source", name.constData(), settings, nullptr);
	if (!source || obs_obj_invalid(source))
		return QStringLiteral("OBS could not create the image source.");
	if (epoch != collectionEpoch || !currentTarget(sceneSource))
		return QStringLiteral("The target scene changed; paste was cancelled.");

	OBSDataAutoRelease savedSource = obs_save_source(source);
	OBSDataAutoRelease action = obs_data_create();
	if (!savedSource || !action)
		return QStringLiteral("OBS could not serialize the image source for undo.");
	obs_data_set_obj(action, "source", savedSource);
	obs_data_set_string(action, "scene_uuid", obs_source_get_uuid(sceneSource));
	obs_data_set_string(action, "source_uuid", obs_source_get_uuid(source));
	obs_data_set_string(action, "collection", collection.toUtf8().constData());
	obs_data_set_int(action, "collection_epoch", epoch);
	obs_data_set_bool(action, "auto_select", options.autoSelect);

	Selection previous;
	obs_scene_enum_items(scene, rememberSelection, &previous);
	OBSSceneItem item = obs_scene_add(scene, source);
	if (!item)
		return QStringLiteral("OBS could not add the image to the target scene.");
	obs_transform_info transform{};
	vec2_set(&transform.pos, float(layout->position.x()), float(layout->position.y()));
	vec2_set(&transform.scale, float(layout->scale), float(layout->scale));
	transform.alignment = OBS_ALIGN_TOP | OBS_ALIGN_LEFT;
	transform.bounds_type = OBS_BOUNDS_NONE;
	obs_sceneitem_set_info2(item, &transform);
	obs_sceneitem_set_order(item, options.bringToFront ? OBS_ORDER_MOVE_TOP : OBS_ORDER_MOVE_BOTTOM);
	applySelection(item, previous, options.autoSelect);
	obs_data_set_int(action, "item_id", obs_sceneitem_get_id(item));
	saveTransform(action, item);
	const char *json = obs_data_get_json(action);
	if (!json || !*json) {
		obs_sceneitem_remove(item);
		for (const auto &entry : previous)
			obs_sceneitem_select(entry.first, entry.second);
		return QStringLiteral("OBS could not serialize the image undo action.");
	}
	// The frontend copies these strings. No source/item/plugin pointers survive
	// in the undo stack. repeatable=false keeps consecutive pastes independent.
	obs_frontend_add_undo_redo_action("Quick Paste: add image", undoCreate, redoCreate, json, json, false);
	obs_frontend_save();
	return {};
}

} // namespace quickpaste
