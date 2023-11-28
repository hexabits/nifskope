#ifndef TOOLDIALOG_H
#define TOOLDIALOG_H

#include <QDialog>
#include <QLayout>
#include <QButtonGroup>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QGroupBox>
#include <QSettings>

using ToolDialogFlagsType = unsigned int;

// Base class for tool dialogs (edits, spells, abouts...).
// Streamlines managing dialog flags and modality, child layouts and widgets, tool's settings, etc.
class ToolDialog : public QWidget
{
public:
	enum Flags : ToolDialogFlagsType
	{
		HResize = 0x01, // Allow horizontal resizing of the dialog
		VResize = 0x02, // Allow vertical resizing of the dialog
		Resize = HResize | VResize, // Allow both horizontal and vertical resizing of the dialog

		NonBlocking = 0x04, // Do NOT block the parent window or the whole application
		ApplicationBlocking = 0x08, // Block the whole application (all other windows)

	};

	ToolDialog( QWidget * parent, const QString & title, ToolDialogFlagsType flags, int startWidth = 0, int startHeight = 0 );

	void open( bool autoDeleteOnClose );

	bool hasToolDialogFlag( Flags flag ) const { return toolDialogFlags & flag; }

protected:
	void closeEvent( QCloseEvent * event ) override;

private:
	ToolDialogFlagsType toolDialogFlags;
	int startWidth;
	int startHeight;


	// Settings helpers
public:
	bool hasSettings() const { return !settingsPath.isEmpty(); }

	void setSettingsFolder( const QString & settingsFolder );

	QString settingsKeyPath( const QString & key ) const
		{ return hasSettings() ? ( settingsPath + key ) : key; }

	QVariant settingsValue( const QString & key, const QVariant & defaultValue = QVariant() )
		{ return settings.value( settingsKeyPath( key ), defaultValue ); }
	int settingsIntValue( const QString & key, int defaultValue = 0 )
		{ return settingsValue( key, defaultValue ).toInt(); }
	QString settingsStrValue( const QString & key, const QString & defaultValue = QString() )
		{ return settingsValue( key, defaultValue ).toString(); }

	void setSettingsValue( const QString & key, const QVariant & value )
		{ settings.setValue( settingsKeyPath( key ), value ); }
	void setSettingsIntValue( const QString & key, int value )
		{ setSettingsValue( key, value ); }
	void setSettingsStrValue( const QString & key, const QString & value )
		{ setSettingsValue( key, value ); }

private:
	QSettings settings;
	QString settingsPath;


	// Layout helpers
public:
	QHBoxLayout * addHBoxLayout( QBoxLayout * attachTo = nullptr );
	QVBoxLayout * addVBoxLayout( QBoxLayout * attachTo = nullptr );

	void addStretch( QBoxLayout * layout, int stretch = 0 ) { layout->addStretch( stretch ); }


	// Label helpers
public:
	QLabel * addLabel( QBoxLayout * parentLayout, const QString & text );


	// Push button helpers
public:
	QPushButton * addPushButton( QBoxLayout * parentLayout, const QString & text );

	void lockPushButtonSize( QPushButton * button );

	QHBoxLayout * beginMainButtonLayout( QBoxLayout * attachTo );
	QPushButton * addMainButton( const QString & text );
	QPushButton * addCloseButton( const QString & text );

private:
	QHBoxLayout * mainButtonLayout = nullptr;
	QVector<QPushButton *> mainButtons;


	// Radio button helpers
public:
	QButtonGroup * beginRadioGroup();

	QRadioButton * addRadioButton( QBoxLayout * parentLayout, const QString & text, int groupId = -1 );

private:
	QButtonGroup * radioGroup = nullptr;


	// Group box helpers
public:
	QGroupBox * addGroupBox( QBoxLayout * parentLayout, const QString & text, QLayout * innerLayout = nullptr );


	// Spin box helpers
public:
	QSpinBox * addSpinBox( QBoxLayout * parentLayout, int minVal, int maxVal, int initVal, int step = 1 );
};

#endif // TOOLDIALOG_H
