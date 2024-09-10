#ifndef TOOLDIALOG_H
#define TOOLDIALOG_H

#include <QLayout>
#include <QGridLayout>
#include <QButtonGroup>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QSettings>
#include <QSizeGrip>

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
	void keyPressEvent( QKeyEvent * event ) override;
	void resizeEvent( QResizeEvent * event ) override;

private:
	ToolDialogFlagsType toolDialogFlags;
	int startWidth;
	int startHeight;
	QSizeGrip * sizeGrip = nullptr;

	void positionSizeGrip();


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
	QHBoxLayout * addHBoxLayout() { return new QHBoxLayout(); }
	QHBoxLayout * addHBoxLayout( QWidget * attachTo );
	QHBoxLayout * addHBoxLayout( QBoxLayout * attachTo );
	QHBoxLayout * addHBoxLayout( QGridLayout * attachTo, int attachColumn );
	QHBoxLayout * addHBoxLayout( QGridLayout * attachTo, int attachColumn, int attachSpan );

	QVBoxLayout * addVBoxLayout() { return new QVBoxLayout(); }
	QVBoxLayout * addVBoxLayout( QWidget * attachTo );
	QVBoxLayout * addVBoxLayout( QBoxLayout * attachTo );
	QVBoxLayout * addVBoxLayout( QGridLayout * attachTo, int attachColumn );
	QVBoxLayout * addVBoxLayout( QGridLayout * attachTo, int attachColumn, int attachSpan );

	void addStretch( QBoxLayout * layout, int stretch = 0 ) { layout->addStretch( stretch ); }

	QGridLayout * addGridLayout();
	QGridLayout * addGridLayout( QWidget * attachTo );
	QGridLayout * addGridLayout( QBoxLayout * attachTo );

	void beginGridRow( const QGridLayout * grid ) { gridLayoutRow = grid->rowCount(); }

private:
	int gridLayoutRow = 0;


	// Dialog helpers (static)
public:
	static void setDialogFlagsAndModality( QWidget * dialog, ToolDialogFlagsType flags );
	static void showDialog( QWidget * dialog );


	// Widget helpers
public:
	void lockWidgetSize( QWidget * widget ) { widget->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed ); }


	// Label helpers
public:
	QLabel * addLabel( const QString & text, bool fixedSize = false );
	QLabel * addLabel( QBoxLayout * parentLayout, const QString & text, bool fixedSize = false );
	QLabel * addLabel( QGridLayout * parentLayout, int layoutColumn, const QString & text, bool fixedSize = false );
	QLabel * addLabel( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, const QString & text, bool fixedSize = false );


	// Push button helpers
public:
	QPushButton * addPushButton( const QString & text );
	QPushButton * addPushButton( QBoxLayout * parentLayout, const QString & text );
	QPushButton * addPushButton( QGridLayout * parentLayout, int layoutColumn, const QString & text );
	QPushButton * addPushButton( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, const QString & text );

	void lockPushButtonSize( QPushButton * button );

	QHBoxLayout * beginMainButtonLayout();
	QHBoxLayout * beginMainButtonLayout( QBoxLayout * attachTo );
	QHBoxLayout * beginMainButtonLayout( QGridLayout * attachTo );
	QPushButton * addMainButton( const QString & text, bool isDefaultButton );
	QPushButton * addCloseButton( const QString & text );

private:
	QHBoxLayout * mainButtonLayout = nullptr;
	QVector<QPushButton *> mainButtons;
	QPushButton * defaultButton = nullptr;


	// Check box helpers
public:
	QCheckBox * addCheckBox( const QString & text );
	QCheckBox * addCheckBox( QBoxLayout * parentLayout, const QString & text );
	QCheckBox * addCheckBox( QGridLayout * parentLayout, int layoutColumn, const QString & text );
	QCheckBox * addCheckBox( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, const QString & text );


	// Radio button helpers
public:
	QButtonGroup * beginRadioGroup();

	QRadioButton * addRadioButton( const QString & text, int groupId = -1 );
	QRadioButton * addRadioButton( QBoxLayout * parentLayout, const QString & text, int groupId = -1 );
	QRadioButton * addRadioButton( QGridLayout * parentLayout, int layoutColumn, const QString & text, int groupId = -1 );
	QRadioButton * addRadioButton( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, const QString & text, int groupId = -1 );

private:
	QButtonGroup * radioGroup = nullptr;


	// Group box helpers
public:
	QGroupBox * addGroupBox( const QString & text, QLayout * innerLayout = nullptr );
	QGroupBox * addGroupBox( QBoxLayout * parentLayout, const QString & text, QLayout * innerLayout = nullptr );
	QGroupBox * addGroupBox( QGridLayout * parentLayout, int layoutColumn, const QString & text, QLayout * innerLayout = nullptr );
	QGroupBox * addGroupBox( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, const QString & text, QLayout * innerLayout = nullptr );


	// Spin box helpers
public:
	QSpinBox * addSpinBox( int minVal, int maxVal, int initVal, int step = 1 );
	QSpinBox * addSpinBox( QBoxLayout * parentLayout, int minVal, int maxVal, int initVal, int step = 1 );
	QSpinBox * addSpinBox( QGridLayout * parentLayout, int layoutColumn, int minVal, int maxVal, int initVal, int step = 1 );
	QSpinBox * addSpinBox( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, int minVal, int maxVal, int initVal, int step = 1 );

	QDoubleSpinBox * addDoubleSpinBox( double minVal, double maxVal, double initVal, int decimalDigits, double step = 1.0 );
	QDoubleSpinBox * addDoubleSpinBox( QBoxLayout * parentLayout, double minVal, double maxVal, double initVal, int decimalDigits, double step = 1.0 );
	QDoubleSpinBox * addDoubleSpinBox( QGridLayout * parentLayout, int layoutColumn, double minVal, double maxVal, double initVal, int decimalDigits, double step = 1.0 );
	QDoubleSpinBox * addDoubleSpinBox( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, double minVal, double maxVal, double initVal, int decimalDigits, double step = 1.0 );
};

#endif // TOOLDIALOG_H
