#ifndef UIUTILS_H
#define UIUTILS_H

#include <QWidget>

//! 
class UIUtils
{
	// Application title
public:
	static QString applicationDisplayName;

	static const QString windowTitleSeparator;

private:
	static void setWindowTitle( QWidget * windowWidget, const QString * titlePart1, const QString * titlePart2, const QString * titlePart3 );
public:
	static void setWindowTitle( QWidget * windowWidget )
		{ setWindowTitle( windowWidget, nullptr, nullptr, nullptr ); }
	static void setWindowTitle( QWidget * windowWidget, const QString & title )
		{ setWindowTitle( windowWidget, &title, nullptr, nullptr ); }
	static void setWindowTitle( QWidget * windowWidget, const QString & title1, const QString & title2 )
		{ setWindowTitle( windowWidget, &title1, &title2, nullptr ); }
	static void setWindowTitle( QWidget * windowWidget, const QString & title1, const QString & title2, const QString & title3 )
		{ setWindowTitle( windowWidget, &title1, &title2, &title3 ); }


	// System UI scale
public:
	static double widgetUIScaleFactor( QWidget * widget );

	//! Returns the real size of a QWidget (what's called the client rectangle in Windows) in pixels.
	// This is a workaround for QWidget::size(), QWidget::width(), etc. returning real pixels divided by the system UI scale, with all the rounding errors accompanying that.
	static QSize widgetRealSize( QWidget * widget );
};

#endif // UIUTILS_H
