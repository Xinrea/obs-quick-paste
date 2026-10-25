#pragma once

#include <QPointF>
#include <QSize>

#include <optional>

namespace quickpaste {

struct SourceLayout {
	double scale = 1.0;
	QPointF position;

	// Never enlarge a small image. Invalid dimensions/ratios have no layout.
	static std::optional<SourceLayout> calculate(const QSize &imageSize, const QSize &canvasSize,
						    bool autoCenter = true, double maxCanvasRatio = 0.8);
};

} // namespace quickpaste
