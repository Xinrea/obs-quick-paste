#include "source-layout.hpp"

#include <QtTest>

#include <cmath>
#include <limits>

using quickpaste::SourceLayout;

class SourceLayoutTest : public QObject {
	Q_OBJECT

private slots:
	void fit_data()
	{
		QTest::addColumn<QSize>("image");
		QTest::addColumn<QSize>("canvas");
		QTest::addColumn<bool>("center");
		QTest::addColumn<double>("ratio");
		QTest::addColumn<double>("scale");
		QTest::addColumn<QPointF>("position");
		QTest::newRow("landscape") << QSize(3840, 2160) << QSize(1920, 1080) << true << 0.8 << 0.4
					    << QPointF(192, 108);
		QTest::newRow("portrait") << QSize(1000, 2000) << QSize(1920, 1080) << true << 0.8 << 0.432
					   << QPointF(744, 108);
		QTest::newRow("small-native-size") << QSize(100, 50) << QSize(1920, 1080) << true << 0.8 << 1.0
						    << QPointF(910, 515);
		QTest::newRow("origin") << QSize(3840, 2160) << QSize(1920, 1080) << false << 0.8 << 0.4
					 << QPointF(0, 0);
		QTest::newRow("native-vertical-canvas") << QSize(1920, 1080) << QSize(1080, 1920) << true << 0.8
						       << 0.45 << QPointF(108, 717);
		QTest::newRow("full-canvas") << QSize(1920, 1080) << QSize(1920, 1080) << true << 1.0 << 1.0
					      << QPointF(0, 0);
		QTest::newRow("odd-dimensions") << QSize(3, 5) << QSize(10, 10) << true << 0.8 << 1.0
						 << QPointF(3.5, 2.5);
		QTest::newRow("wide") << QSize(4000, 100) << QSize(1000, 1000) << true << 0.5 << 0.125
				       << QPointF(250, 493.75);
	}

	void fit()
	{
		QFETCH(QSize, image);
		QFETCH(QSize, canvas);
		QFETCH(bool, center);
		QFETCH(double, ratio);
		QFETCH(double, scale);
		QFETCH(QPointF, position);
		const auto result = SourceLayout::calculate(image, canvas, center, ratio);
		QVERIFY(result.has_value());
		QVERIFY(std::abs(result->scale - scale) < 1e-12);
		QVERIFY(std::abs(result->position.x() - position.x()) < 1e-9);
		QVERIFY(std::abs(result->position.y() - position.y()) < 1e-9);
	}

	void invalidInputs()
	{
		const QSize valid(1920, 1080);
		for (const QSize &invalid : {QSize(), QSize(0, 10), QSize(10, 0), QSize(-1, 10), QSize(10, -1)}) {
			QVERIFY(!SourceLayout::calculate(invalid, valid));
			QVERIFY(!SourceLayout::calculate(valid, invalid));
		}
		for (double ratio : {0.0, -0.1, 1.01, std::numeric_limits<double>::infinity(),
				     -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
			QVERIFY(!SourceLayout::calculate(valid, valid, true, ratio));
	}

	void extremeDimensions()
	{
		const int maximum = std::numeric_limits<int>::max();
		const auto result = SourceLayout::calculate(QSize(maximum, maximum), QSize(maximum, maximum));
		QVERIFY(result.has_value());
		QVERIFY(std::abs(result->scale - 0.8) < 1e-12);
		QVERIFY(std::isfinite(result->position.x()));
		QVERIFY(result->position.x() > 0);
	}
};

QTEST_APPLESS_MAIN(SourceLayoutTest)
#include "test-source-layout.moc"
