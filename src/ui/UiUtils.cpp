#include "UiUtils.h"

#include <QWindow>
#include <QGuiApplication>
#include <QScreen>
#include <QStringBuilder>
#include <QDebug>

#ifdef Q_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#endif

QString UIUtils::applicationDisplayName;
const QString UIUtils::windowTitleSeparator(" - ");

void UIUtils::setWindowTitle( QWidget * windowWidget, const QString * titlePart1, const QString * titlePart2, const QString * titlePart3 )
{
	const QString * goodParts[3];
	int nGoodParts = 0;
	auto addGoodPart = [&goodParts, &nGoodParts]( const QString * part ) {
		if ( part && !part->isEmpty() ) {
			goodParts[nGoodParts] = part;
			nGoodParts++;
		}
	};
	addGoodPart( titlePart1 );
	addGoodPart( titlePart2 );
	addGoodPart( titlePart3 );

	switch( nGoodParts ) {
	case 3:
		windowWidget->setWindowTitle( *goodParts[0] % windowTitleSeparator % *goodParts[1] % windowTitleSeparator % *goodParts[2] );
		break;
	case 2:
		windowWidget->setWindowTitle( *goodParts[0] % windowTitleSeparator % *goodParts[1] );
		break;
	case 1:
		windowWidget->setWindowTitle( *goodParts[0] );
		break;
	default:
		windowWidget->setWindowTitle( applicationDisplayName );
		break;
	}
}


double UIUtils::widgetUIScaleFactor( QWidget * widget )
{
	QWidget * window = widget->window();
	if ( window ) {
		QWindow * winHandle = window->windowHandle();
		if ( winHandle ) {
			double scaleFactor = winHandle->devicePixelRatio();
			if ( scaleFactor > 0.0 )
				return scaleFactor;

		} else {
			// winHandle is null, so the window has not been created yet

			// Let's try the parent window of this window first...
			QWidget * parentWindow = window->parentWidget();
			if ( parentWindow )
				return widgetUIScaleFactor( parentWindow );

			// Last resort: the scale factor of the primary (default) screen.
			QScreen * screen = QGuiApplication::primaryScreen();
			if ( screen ) {
				double scaleFactor = screen->devicePixelRatio();
				if ( scaleFactor > 0.0 )
					return scaleFactor;
			}

		}
	}

	return 1.0;
}

QSize UIUtils::widgetRealSize( QWidget * widget )
{
#ifdef Q_OS_WIN32
	RECT wrect;
	if ( GetClientRect( (HWND) widget->winId(), &wrect ) )
		return QSize( wrect.right, wrect.bottom );
#endif

	double scaleFactor = widgetUIScaleFactor( widget );
	if ( scaleFactor != 1.0 )
		return QSize( qRound( widget->width() * scaleFactor ), qRound( widget->height() * scaleFactor ) );

	return widget->size();
}
