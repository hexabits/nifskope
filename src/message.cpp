#include "message.h"

#include <QApplication>
#include <QAbstractButton>
#include <QMap>
#include <QCloseEvent>
#include <QScreen>
#include <QTimer>

#include "ui/UiUtils.h"


Q_LOGGING_CATEGORY( ns, "nifskope" )
Q_LOGGING_CATEGORY( nsGl, "nifskope.gl" )
Q_LOGGING_CATEGORY( nsIo, "nifskope.io" )
Q_LOGGING_CATEGORY( nsNif, "nifskope.nif" )
Q_LOGGING_CATEGORY( nsSpell, "nifskope.spell" )


Message::Message() : QObject( nullptr )
{

}

Message::~Message()
{

}

//! Static helper for message box without detail text
QMessageBox* Message::message( QWidget * parent, const QString & str, QMessageBox::Icon icon )
{
	auto msgBox = new QMessageBox( parent );
	msgBox->setWindowFlags( msgBox->windowFlags() | Qt::Tool );
	msgBox->setAttribute( Qt::WA_DeleteOnClose );
	msgBox->setWindowModality( Qt::NonModal );
	UIUtils::setWindowTitle( msgBox );

	msgBox->setText( str );
	msgBox->setIcon( icon );

	msgBox->show();

	msgBox->activateWindow();

	return msgBox;
}

//! Static helper for message box with detail text
QMessageBox* Message::message( QWidget * parent, const QString & str, const QString & err, QMessageBox::Icon icon )
{
	if ( !parent )
		parent = qApp->activeWindow();

	auto msgBox = new QMessageBox( parent );
	msgBox->setAttribute( Qt::WA_DeleteOnClose );
	msgBox->setWindowModality( Qt::NonModal );
	msgBox->setWindowFlags( msgBox->windowFlags() | Qt::Tool );
	UIUtils::setWindowTitle( msgBox );

	msgBox->setText( str );
	msgBox->setIcon( icon );
	msgBox->setDetailedText( err );

	msgBox->show();

	msgBox->activateWindow();

	return msgBox;
}

//! Static helper for installed message handler
void Message::message( QWidget * parent, const QString & str, const QMessageLogContext * context, QMessageBox::Icon icon )
{

#ifdef QT_NO_DEBUG
	if ( !QString( context->category ).startsWith( "nifskope", Qt::CaseInsensitive ) ) {
		
		// TODO: Log these into a log table widget like Qt Creator's issues tab

		return;
	}
#endif

	QString d;
	d.append( QString( "%1: %2\n" ).arg( "File" ).arg( context->file ) );
	d.append( QString( "%1: %2\n" ).arg( "Function" ).arg( context->function ) );
	d.append( QString( "%1: %2\n" ).arg( "Line" ).arg( context->line ) );
	d.append( QString( "%1: %2\n" ).arg( "Category" ).arg( context->category ) );
	d.append( QString( "%1:\n\n%2" ).arg( "Message" ).arg( str ) );

	message( parent, str, d, icon );
}

/*
* Critical message boxes
*
*/

void Message::critical( QWidget * parent, const QString & str )
{
	message( parent, str, QMessageBox::Critical );
}

void Message::critical( QWidget * parent, const QString & str, const QString & err )
{
	message( parent, str, err, QMessageBox::Critical );
}

/*
* Warning message boxes
*
*/

void Message::warning( QWidget * parent, const QString & str )
{
	message( parent, str, QMessageBox::Warning );
}

void Message::warning( QWidget * parent, const QString & str, const QString & err )
{
	message( parent, str, err, QMessageBox::Warning );
}

/*
* Info message boxes
*
*/

void Message::info( QWidget * parent, const QString & str )
{
	message( parent, str, QMessageBox::Information );
}

void Message::info( QWidget * parent, const QString & str, const QString & err )
{
	message( parent, str, err, QMessageBox::Information );
}

/*
* Accumulate messages in detailed text area
*
*/

class DetailsMessageBox : public QMessageBox
{
public:
	explicit DetailsMessageBox( QWidget * parent, const QString & txt )
		: QMessageBox( parent ), msgKey( txt )
	{
		UIUtils::setWindowTitle( this );

		detailFlushTimer = new QTimer( this );
		detailFlushTimer->setSingleShot( true );
		detailFlushTimer->setInterval( 20 );
		connect( detailFlushTimer, &QTimer::timeout, this, &DetailsMessageBox::flushDetailBuffer );
	}

	~DetailsMessageBox();

	const QString & key() const { return msgKey; }

	void setFirstDetail( const QString & detailText )
	{
		if ( !detailText.isEmpty() ) {
			detailBuffer = detailText + "\n";
			setDetailedText( detailBuffer );
		} else {
			// detailText is empty, set the detailed text to a dummy, just for the sake of setting up "Show/Hide Details".
			setDetailedText( " \n" ); 
			detailBuffer.clear(); // Just in case...
		}
	
		// Auto-show detailed text on first show.
		// https://stackoverflow.com/questions/36083551/qmessagebox-show-details
		for ( auto btn : buttons() ) {
			if ( buttonRole( btn ) == QMessageBox::ActionRole ) {
				btn->click(); // "Click" it to expand the detailed text
				break;
			}
		}
	}

	void appendDetail( const QString & detailText )
	{
		if ( detailText.isEmpty() )
			return;

		detailBuffer.append( detailText + "\n" );
		if ( !detailFlushTimer->isActive() )
			detailFlushTimer->start();
	}

protected slots:
	void flushDetailBuffer()
	{
		setDetailedText( detailBuffer );
	}

protected:
	void closeEvent( QCloseEvent * event ) override;

	QString msgKey;
	QString detailBuffer;
	QTimer * detailFlushTimer;
};

static QVector<DetailsMessageBox *> messageBoxes;

void unregisterMessageBox( DetailsMessageBox * msgBox ) 
{
	messageBoxes.removeOne( msgBox );
}

void Message::append( QWidget * parent, const QString & str, const QString & err, QMessageBox::Icon icon )
{
	if ( !parent )
		parent = qApp->activeWindow();

	// Create one box per parent widget and error string, accumulate messages
	DetailsMessageBox * msgBox = nullptr;
	for ( auto box : messageBoxes ) {
		if ( box->parentWidget() == parent && box->key() == str ) {
			msgBox = box;
			break;
		}
	}

	if ( msgBox ) {
		msgBox->appendDetail( err );

	} else {
		// Create new message box
		msgBox = new DetailsMessageBox( parent, str );
		messageBoxes.append( msgBox );

		msgBox->setAttribute( Qt::WA_DeleteOnClose );
		msgBox->setWindowModality( Qt::NonModal );
		msgBox->setWindowFlags( msgBox->windowFlags() | Qt::Tool );

		// Set the min. width of the label containing str to a quarter of the screen resolution.
		// This makes the detailed text more readable even when str is short.
		auto screen = QGuiApplication::primaryScreen();
		if ( screen ) {
			msgBox->setStyleSheet( 
				QString(" QLabel[objectName^=\"qt_msgbox_label\"]{min-width: %1px;}" )
				.arg ( screen->size().width() / 4 )
			);
		}

		msgBox->setText( str );
		msgBox->setIcon( icon );

		connect( msgBox, &QMessageBox::buttonClicked, [msgBox]( [[maybe_unused]] QAbstractButton * button ) { 
			unregisterMessageBox( msgBox );
			} );

		msgBox->show();

		// setDetailedText() in setFirstDetail() has to be called after show(),
		// otherwise a "QWindowsWindow::setGeometry: Unable to set geometry ..." warning from Qt appears in Debug build.
		msgBox->setFirstDetail( err );

		msgBox->activateWindow();
	}
}

void Message::append( const QString & str, const QString & err, QMessageBox::Icon icon )
{
	append( nullptr, str, err, icon );
}

DetailsMessageBox::~DetailsMessageBox()
{
	unregisterMessageBox( this ); // Just in case if buttonClicked or closeEvent fail
}

void DetailsMessageBox::closeEvent( QCloseEvent * event )
{	
	QMessageBox::closeEvent( event );
	if ( event->isAccepted() )
		unregisterMessageBox( this );
}


/*
 Old message class
*/

inline void space( QString & s )
{
	if ( !s.isEmpty() )
		s += " ";
}

template <> TestMessage & TestMessage::operator<<(const char * x)
{
	space( s );
	s += x;
	return *this;
}

template <> TestMessage & TestMessage::operator<<(QString x)
{
	space( s );
	s += x; //"\"" + x + "\"";
	return *this;
}

template <> TestMessage & TestMessage::operator<<(QByteArray x)
{
	space( s );
	s += "\"" + x + "\"";
	return *this;
}

template <> TestMessage & TestMessage::operator<<(int x)
{
	space( s );
	s += QString::number( x );
	return *this;
}

template <> TestMessage & TestMessage::operator<<(unsigned int x)
{
	space( s );
	s += QString::number( x );
	return *this;
}

template <> TestMessage & TestMessage::operator<<(double x)
{
	space( s );
	s += QString::number( x );
	return *this;
}

template <> TestMessage & TestMessage::operator<<(float x)
{
	space( s );
	s += QString::number( x );
	return *this;
}
