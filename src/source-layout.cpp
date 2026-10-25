#include "source-layout.hpp"

#include <algorithm>
#include <cmath>

namespace quickpaste {

std::optional<SourceLayout> SourceLayout::calculate(const QSize &imageSize, const QSize &canvasSize, bool autoCenter,
						   double maxCanvasRatio)
{
	if (imageSize.width() <= 0 || imageSize.height() <= 0 || canvasSize.width() <= 0 || canvasSize.height() <= 0 ||
	    !std::isfinite(maxCanvasRatio) || maxCanvasRatio <= 0.0 || maxCanvasRatio > 1.0)
		return std::nullopt;

	SourceLayout layout;
	layout.scale = std::min({1.0, canvasSize.width() * maxCanvasRatio / imageSize.width(),
				 canvasSize.height() * maxCanvasRatio / imageSize.height()});
	if (autoCenter) {
		layout.position = QPointF((canvasSize.width() - imageSize.width() * layout.scale) / 2.0,
					  (canvasSize.height() - imageSize.height() * layout.scale) / 2.0);
	}
	return layout;
}

} // namespace quickpaste
