#pragma once

#include <QSize>
#include <QString>

namespace quickpaste {

struct SourceOptions {
	bool autoCenter = true;
	double maxCanvasRatio = 0.8;
	bool autoSelect = true;
	// False places the image at the bottom; obs_scene_add otherwise adds on top.
	bool bringToFront = true;
};

class OBSSourceFactory {
public:
	// UI thread only, after persistence. Empty result means success. The caller
	// may delete the persisted file on failure; undo/redo never delete files.
	static QString create(const QString &absoluteImagePath, const QSize &imageSize, const QString &targetSceneUuid,
			      const SourceOptions &options);

	// Call on the UI thread from obs_module_unload. Removes the collection
	// callback and invalidates existing actions. OBS has no public API to remove
	// those undo callbacks: hot-unloading/reloading still requires an OBS restart.
	static void shutdown();
};

} // namespace quickpaste
