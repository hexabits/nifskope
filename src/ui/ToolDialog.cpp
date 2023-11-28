#include "ToolDialog.h"
#include <QDebug>
#include <QStringBuilder>
#include <QCloseEvent>

const QString SETTING_WIDTH( "DialogWidth" );
const QString SETTING_HEIGHT( "DialogHeight" );

const int PUSH_BUTTON_WIDTH_PADDING  = 8;
const int PUSH_BUTTON_HEIGHT_PADDING = 0;


ToolDialog::ToolDialog( QWidget * parent, const QString & title, ToolDialogFlagsType flags, int startWidth, int startHeight )
	: QWidget( parent ? parent->window() : nullptr, 0 ), toolDialogFlags( flags ), startWidth( startWidth ), startHeight( startHeight )
{
	setWindowTitle( title );
}

void ToolDialog::open( bool autoDeleteOnClose )
{
	auto parentW = parentWidget();

	setAttribute( Qt::WA_DeleteOnClose, autoDeleteOnClose );

	// Finalize main button layout
	if ( mainButtonLayout ) {
		int maxWidth = 100, maxHeight = 0;
		for ( auto button : mainButtons ) {
			auto testSize = button->sizeHint();
			auto testWidth = testSize.width() + PUSH_BUTTON_WIDTH_PADDING;
			if ( testWidth > maxWidth )
				maxWidth = testWidth;
			auto testHeight = testSize.height() + PUSH_BUTTON_HEIGHT_PADDING;
			if ( testHeight > maxHeight )
				maxHeight = testHeight;
		}
		for ( auto button : mainButtons )
			button->setFixedSize( maxWidth, maxHeight );
	}
	mainButtons.clear(); // Don't need it anymore

	// Window modality
	bool isNonModal = hasToolDialogFlag( Flags::NonBlocking ) && parentW;

	Qt::WindowFlags winFlags = Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint;
	Qt::WindowModality winModality;
	if ( isNonModal ) {
		winFlags |= Qt::Tool;
		winModality = Qt::WindowModality::NonModal;
	} else {
		winFlags |= Qt::Dialog | Qt::WindowSystemMenuHint;
		if ( hasToolDialogFlag( Flags::ApplicationBlocking ) || !parentW )
			winModality = Qt::WindowModality::ApplicationModal;
		else
			winModality = Qt::WindowModality::WindowModal;
	}
	setWindowFlags( winFlags );
	setWindowModality( winModality );

	// Setting sizes
	QSize szHint = sizeHint();
	int minWidth = szHint.width(), minHeight = szHint.height();

	// The setMinimum...(...) calls below at first glance are redundant because the size of a window can't get smaller than sizeHint() anyway,
	// but those setMinimum... poke something within Qt 5 that properly centers the window on show().
	if ( hasToolDialogFlag( Flags::HResize ) )
		setMinimumWidth( minWidth );
	else
		setFixedWidth( minWidth );
	if ( hasToolDialogFlag( Flags::VResize ) )
		setMinimumHeight( minHeight );
	else
		setFixedHeight( minHeight );

	auto getCustomDimension = [this]( int minDim, int startDim, Flags resizeFlag, const QString & settingName ) {
		if ( hasToolDialogFlag( resizeFlag ) && hasSettings() ) {
			int savedDim = settingsIntValue( settingName, 0 );
			if ( savedDim >= minDim )
				return savedDim;
		}
		if ( startDim > minDim )
			return startDim;
		return minDim;
	};

	int customWidth = getCustomDimension( minWidth, startWidth, Flags::HResize, SETTING_WIDTH );
	int customHeight = getCustomDimension( minHeight, startHeight, Flags::VResize, SETTING_HEIGHT );
	if ( customWidth > minWidth || customHeight > minHeight )
		resize( customWidth, customHeight );

	// Show and activate me
	show();
	if ( isNonModal )
		activateWindow();
}

void ToolDialog::closeEvent( QCloseEvent * event )
{
	QSize sz = size();
	QWidget::closeEvent( event );
	if ( event->isAccepted() && hasSettings() ) {
		auto saveDimension = [this]( int dim, Flags resizeFlag, const QString & settingName ) {
			if ( hasToolDialogFlag( resizeFlag ) )
				setSettingsIntValue( settingName, dim );
			else
				settings.remove( settingsKeyPath( settingName ) );
		};

		saveDimension( sz.width(), Flags::HResize, SETTING_WIDTH );
		saveDimension( sz.height(), Flags::VResize, SETTING_HEIGHT );
	}
}

void ToolDialog::setSettingsFolder( const QString & settingsFolder )
{
	if ( settingsFolder.isEmpty() ) {
		settingsPath.clear();
	} else {
		settingsPath = QStringLiteral("Dialogs/") % settingsFolder % QStringLiteral("/");
	}
}


// Layout helpers

static inline void attachLayoutToLayout( QLayout * childLayout, QBoxLayout * parentLayout )
{
	if ( parentLayout )
		parentLayout->addLayout( childLayout );
}

QHBoxLayout * ToolDialog::addHBoxLayout( QBoxLayout * attachTo )
{
	auto layout = new QHBoxLayout();
	attachLayoutToLayout( layout, attachTo );
	return layout;
}

QVBoxLayout * ToolDialog::addVBoxLayout( QBoxLayout * attachTo )
{
	auto layout = new QVBoxLayout();
	attachLayoutToLayout( layout, attachTo );
	return layout;
}

static inline QWidget * getWidgetCreateParent( QWidget * parentWidget, QLayout * parentLayout )
{
	return parentLayout ? nullptr : parentWidget;
}

static inline void attachWidgetToLayout( QWidget * widget, QBoxLayout * layout )
{
	if ( layout )
		layout->addWidget( widget );
}


// Label helpers

static inline QLabel * createLabel( const QString & text, QWidget * parentWidget, QLayout * parentLayout )
{
	return new QLabel( text, getWidgetCreateParent( parentWidget, parentLayout ) );
}

QLabel * ToolDialog::addLabel( QBoxLayout * parentLayout, const QString & text )
{
	QLabel * label = createLabel( text, this, parentLayout );
	attachWidgetToLayout( label, parentLayout );
	return label;
}


// Push button helpers

static inline QPushButton * createPushButton( const QString & text, QWidget * parentWidget, QLayout * parentLayout )
{
	auto button = new QPushButton( text, getWidgetCreateParent( parentWidget, parentLayout ) );
	button->setAutoDefault( false );
	return button;
}

QPushButton * ToolDialog::addPushButton( QBoxLayout * parentLayout, const QString & text )
{
	QPushButton * button = createPushButton( text, this, parentLayout );
	attachWidgetToLayout( button, parentLayout );
	return button;
}

void ToolDialog::lockPushButtonSize( QPushButton * button )
{
	QSize szHint = button->sizeHint();
	button->setFixedSize( szHint.width() + PUSH_BUTTON_WIDTH_PADDING, szHint.height() + PUSH_BUTTON_HEIGHT_PADDING );
}

QHBoxLayout * ToolDialog::beginMainButtonLayout( QBoxLayout * attachTo )
{
	if ( !mainButtonLayout )
		mainButtonLayout = addHBoxLayout( attachTo );
	return mainButtonLayout;
}

QPushButton * ToolDialog::addMainButton( const QString & text )
{
	if ( mainButtonLayout && mainButtons.count() == 0)
		mainButtonLayout->addStretch( 1 );

	QPushButton * button = addPushButton( mainButtonLayout, text );
	mainButtons.append( button );
	return button;
}

QPushButton * ToolDialog::addCloseButton( const QString & text )
{
	QPushButton * button = addMainButton( text );
	connect( button, &QPushButton::clicked, this, &QWidget::close );
	return button;
}


// Group box helpers

static inline QGroupBox * createGroupBox( const QString & text, QLayout * innerLayout, QWidget * parentWidget, QLayout * parentLayout )
{
	auto groupBox = new QGroupBox( text, getWidgetCreateParent( parentWidget, parentLayout ) );
	if ( innerLayout )
		groupBox->setLayout( innerLayout );
	return groupBox;
}

QGroupBox * ToolDialog::addGroupBox( QBoxLayout * parentLayout, const QString & text, QLayout * innerLayout )
{
	QGroupBox * groupBox = createGroupBox( text, innerLayout, this, parentLayout );
	attachWidgetToLayout( groupBox, parentLayout );
	return groupBox;
}


// Spin box helpers

static inline QSpinBox * createSpinBox( int minVal, int maxVal, int initVal, int step, QWidget * parentWidget, QLayout * parentLayout )
{
	QSpinBox * spinBox = new QSpinBox( getWidgetCreateParent( parentWidget, parentLayout ) );
	spinBox->setRange( minVal, maxVal );
	spinBox->setValue( initVal );
	spinBox->setSingleStep( step );
	return spinBox;
}

QSpinBox * ToolDialog::addSpinBox( QBoxLayout * parentLayout, int minVal, int maxVal, int initVal, int step )
{
	QSpinBox * spinBox = createSpinBox( minVal, maxVal, initVal, step, this, parentLayout );
	attachWidgetToLayout( spinBox, parentLayout );
	return spinBox;
}


// Radio button helpers

QButtonGroup * ToolDialog::beginRadioGroup()
{
	radioGroup = new QButtonGroup( this );
	radioGroup->setExclusive( true );
	return radioGroup;
}

static inline QRadioButton * createRadioButton( const QString & text, int groupId, QButtonGroup * radioGroup, QWidget * parentWidget, QLayout * parentLayout )
{
	QRadioButton * button = new QRadioButton( text, getWidgetCreateParent( parentWidget, parentLayout ) );
	if ( radioGroup )
		radioGroup->addButton( button, groupId );
	return button;
}

QRadioButton * ToolDialog::addRadioButton( QBoxLayout * parentLayout, const QString & text, int groupId )
{
	QRadioButton * button = createRadioButton( text, groupId, radioGroup, this, parentLayout );
	attachWidgetToLayout( button, parentLayout );
	return button;
}

