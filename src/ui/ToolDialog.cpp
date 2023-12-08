#include "ToolDialog.h"

#include <QDebug>
#include <QStringBuilder>
#include <QCloseEvent>

#include "UiUtils.h"

const QString SETTING_WIDTH( "DialogWidth" );
const QString SETTING_HEIGHT( "DialogHeight" );

const int PUSH_BUTTON_WIDTH_PADDING  = 8;
const int PUSH_BUTTON_HEIGHT_PADDING = 0;


ToolDialog::ToolDialog( QWidget * parent, const QString & title, ToolDialogFlagsType flags, int startWidth, int startHeight )
	: QWidget( parent ? parent->window() : nullptr, 0 ), toolDialogFlags( flags ), startWidth( startWidth ), startHeight( startHeight )
{
	UIUtils::setWindowTitle( this, title );
}

void ToolDialog::open( bool autoDeleteOnClose )
{
	QWidget * parentW = parentWidget();

	setAttribute( Qt::WA_DeleteOnClose, autoDeleteOnClose );

	// Finalize main button layout
	if ( !defaultButton && mainButtons.count() == 1 )
		defaultButton = mainButtons[0];
	if ( defaultButton )
		defaultButton->setDefault( true );

	if ( mainButtonLayout ) {
		int newButtonWidth = 100, newButtonHeight = 0;
		for ( QPushButton * button : mainButtons ) {
			auto testSize = button->sizeHint();
			auto testWidth = testSize.width() + PUSH_BUTTON_WIDTH_PADDING;
			if ( testWidth > newButtonWidth )
				newButtonWidth = testWidth;
			auto testHeight = testSize.height() + PUSH_BUTTON_HEIGHT_PADDING;
			if ( testHeight > newButtonHeight )
				newButtonHeight = testHeight;
		}
		for ( auto button : mainButtons )
			button->setFixedSize( newButtonWidth, newButtonHeight );

		// Add small margin at the top to visually split the main buttons from the rest of widgets
		QMargins margins = mainButtonLayout->contentsMargins();
		margins.setTop( margins.top() + 4 );
		mainButtonLayout->setContentsMargins( margins );
	}

	mainButtons.clear(); // Don't need it anymore

	// Window flags and modality
	bool isNonModal = hasToolDialogFlag( Flags::NonBlocking ) && parentW;

	Qt::WindowFlags winFlags = Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint;
	Qt::WindowModality winModality = Qt::WindowModality::WindowModal;
	if ( isNonModal ) {
		winFlags |= Qt::Tool;
		winModality = Qt::WindowModality::NonModal;
	} else {
		winFlags |= Qt::Dialog | Qt::WindowSystemMenuHint;
		if ( hasToolDialogFlag( Flags::ApplicationBlocking ) || !parentW )
			winModality = Qt::WindowModality::ApplicationModal;
	}

	if ( !hasToolDialogFlag( Flags::Resize ) )
		winFlags |= Qt::MSWindowsFixedSizeDialogHint;

	setWindowFlags( winFlags );
	setWindowModality( winModality );

	// Setting sizes
	QSize szHint = sizeHint();
	int minWidth = szHint.width(), minHeight = szHint.height();
	if ( startWidth < minWidth )
		startWidth = minWidth;
	if ( startHeight < minHeight )
		startHeight = minHeight;

	// The setMinimum...(...) calls below at first glance are redundant because the size of a window can't get smaller than sizeHint() anyway,
	// but those setMinimum... poke something within Qt (at least in Qt5) that properly centers the window on show().
	if ( hasToolDialogFlag( Flags::HResize ) )
		setMinimumWidth( minWidth );
	else
		setFixedWidth( startWidth );
	if ( hasToolDialogFlag( Flags::VResize ) )
		setMinimumHeight( minHeight );
	else
		setFixedHeight( startHeight );

	auto getCustomDimension = [this]( int minDim, int startDim, Flags resizeFlag, const QString & settingName ) {
		if ( hasToolDialogFlag( resizeFlag ) && hasSettings() ) {
			int savedDim = settingsIntValue( settingName, 0 );
			if ( savedDim >= minDim )
				return savedDim;
		}
		return startDim;
	};

	int customWidth = getCustomDimension( minWidth, startWidth, Flags::HResize, SETTING_WIDTH );
	int customHeight = getCustomDimension( minHeight, startHeight, Flags::VResize, SETTING_HEIGHT );
	if ( customWidth > minWidth || customHeight > minHeight )
		resize( customWidth, customHeight );

	if ( hasToolDialogFlag( Flags::Resize ) ) {
		sizeGrip = new QSizeGrip( this );
		sizeGrip->resize( sizeGrip->sizeHint() );
		positionSizeGrip();
	}

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

void ToolDialog::keyPressEvent( QKeyEvent * event )
{
	// Must implement closing the dialog on Esc and pressing the default button on Enter
	if ( event->matches( QKeySequence::Cancel ) ) {
		close();
		return;
	} else if ( defaultButton && ( event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return ) ) {
		if ( defaultButton->isEnabled() )
			defaultButton->click();
		return;
	}

	event->ignore();
}

void ToolDialog::resizeEvent( [[maybe_unused]] QResizeEvent * event )
{
	positionSizeGrip();
}

void ToolDialog::positionSizeGrip()
{
	if ( sizeGrip ) {
		if ( isRightToLeft() )
			sizeGrip->move( rect().bottomLeft() - sizeGrip->rect().bottomLeft() );
		else
			sizeGrip->move( rect().bottomRight() - sizeGrip->rect().bottomRight() );
		sizeGrip->raise();
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

static inline void attachLayoutToWidget( QLayout * childLayout, QWidget * parentWidget )
{
	if ( parentWidget )
		parentWidget->setLayout( childLayout );
}

static inline void attachLayoutToLayout( QLayout * childLayout, QBoxLayout * parentLayout )
{
	if ( parentLayout )
		parentLayout->addLayout( childLayout );
}

static inline void attachLayoutToLayout( QLayout * childLayout, QGridLayout * parentLayout, int parentRow, int parentColumn )
{
	if ( parentLayout )
		parentLayout->addLayout( childLayout, parentRow, parentColumn );
}

static inline void attachLayoutToLayout( QLayout * childLayout, QGridLayout * parentLayout, int parentRow, int parentColumn, int parentSpan )
{
	if ( parentLayout )
		parentLayout->addLayout( childLayout, parentRow, parentColumn, 1, parentSpan );
}

QHBoxLayout * ToolDialog::addHBoxLayout( QWidget * attachTo )
{
	QHBoxLayout * layout = addHBoxLayout();
	attachLayoutToWidget( layout, attachTo );
	return layout;
}

QHBoxLayout * ToolDialog::addHBoxLayout( QBoxLayout * attachTo )
{
	QHBoxLayout * layout = addHBoxLayout();
	attachLayoutToLayout( layout, attachTo );
	return layout;
}

QHBoxLayout * ToolDialog::addHBoxLayout( QGridLayout * attachTo, int attachColumn )
{
	QHBoxLayout * layout = addHBoxLayout();
	attachLayoutToLayout( layout, attachTo, gridLayoutRow, attachColumn );
	return layout;
}

QHBoxLayout * ToolDialog::addHBoxLayout( QGridLayout * attachTo, int attachColumn, int attachSpan )
{
	QHBoxLayout * layout = addHBoxLayout();
	attachLayoutToLayout( layout, attachTo, gridLayoutRow, attachColumn, attachSpan );
	return layout;
}

QVBoxLayout * ToolDialog::addVBoxLayout( QWidget * attachTo )
{
	QVBoxLayout * layout = addVBoxLayout();
	attachLayoutToWidget( layout, attachTo );
	return layout;
}

QVBoxLayout * ToolDialog::addVBoxLayout( QBoxLayout * attachTo )
{
	QVBoxLayout * layout = addVBoxLayout();
	attachLayoutToLayout( layout, attachTo );
	return layout;
}

QVBoxLayout * ToolDialog::addVBoxLayout( QGridLayout * attachTo, int attachColumn )
{
	QVBoxLayout * layout = addVBoxLayout();
	attachLayoutToLayout( layout, attachTo, gridLayoutRow, attachColumn );
	return layout;
}

QVBoxLayout * ToolDialog::addVBoxLayout( QGridLayout * attachTo, int attachColumn, int attachSpan )
{
	QVBoxLayout * layout = addVBoxLayout();
	attachLayoutToLayout( layout, attachTo, gridLayoutRow, attachColumn, attachSpan );
	return layout;
}

QGridLayout * ToolDialog::addGridLayout()
{
	gridLayoutRow = 0;
	return new QGridLayout();
}

QGridLayout * ToolDialog::addGridLayout( QWidget * attachTo )
{
	QGridLayout * layout = addGridLayout();
	attachLayoutToWidget( layout, attachTo );
	return layout;
}

QGridLayout * ToolDialog::addGridLayout( QBoxLayout * attachTo )
{
	QGridLayout * layout = addGridLayout();
	attachLayoutToLayout( layout, attachTo );
	return layout;
}

static inline void attachWidgetToLayout( QWidget * widget, QBoxLayout * layout )
{
	if ( layout )
		layout->addWidget( widget );
}

static inline void attachWidgetToLayout( QWidget * widget, QGridLayout * layout, int layoutRow, int layoutColumn )
{
	if ( layout )
		layout->addWidget( widget, layoutRow, layoutColumn );
}

static inline void attachWidgetToLayout( QWidget * widget, QGridLayout * layout, int layoutRow, int layoutColumn, int layoutSpan )
{
	if ( layout )
		layout->addWidget( widget, layoutRow, layoutColumn, 1, layoutSpan );
}


// Label helpers

QLabel * ToolDialog::addLabel( const QString & text, bool fixedSize )
{
	QLabel * label = new QLabel( text );
	if ( fixedSize )
		lockWidgetSize( label );
	return label;
}

QLabel * ToolDialog::addLabel( QBoxLayout * parentLayout, const QString & text, bool fixedSize )
{
	QLabel * label = addLabel( text, fixedSize );
	attachWidgetToLayout( label, parentLayout );
	return label;
}

QLabel * ToolDialog::addLabel( QGridLayout * parentLayout, int layoutColumn, const QString & text, bool fixedSize )
{
	QLabel * label = addLabel( text, fixedSize );
	attachWidgetToLayout( label, parentLayout, gridLayoutRow, layoutColumn );
	return label;
}

QLabel * ToolDialog::addLabel( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, const QString & text, bool fixedSize )
{
	QLabel * label = addLabel( text, fixedSize );
	attachWidgetToLayout( label, parentLayout, gridLayoutRow, layoutColumn, layoutSpan );
	return label;
}


// Push button helpers

QPushButton * ToolDialog::addPushButton( const QString & text )
{
	auto button = new QPushButton( text );
	button->setAutoDefault( false );
	return button;
}

QPushButton * ToolDialog::addPushButton( QBoxLayout * parentLayout, const QString & text )
{
	QPushButton * button = addPushButton( text );
	attachWidgetToLayout( button, parentLayout );
	return button;
}

QPushButton * ToolDialog::addPushButton( QGridLayout * parentLayout, int layoutColumn, const QString & text )
{
	QPushButton * button = addPushButton( text );
	attachWidgetToLayout( button, parentLayout, gridLayoutRow, layoutColumn );
	return button;
}

QPushButton * ToolDialog::addPushButton( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, const QString & text )
{
	QPushButton * button = addPushButton( text );
	attachWidgetToLayout( button, parentLayout, gridLayoutRow, layoutColumn, layoutSpan );
	return button;
}

void ToolDialog::lockPushButtonSize( QPushButton * button )
{
	QSize szHint = button->sizeHint();
	button->setFixedSize( szHint.width() + PUSH_BUTTON_WIDTH_PADDING, szHint.height() + PUSH_BUTTON_HEIGHT_PADDING );
}

QHBoxLayout * ToolDialog::beginMainButtonLayout()
{
	if ( !mainButtonLayout )
		mainButtonLayout = addHBoxLayout();
	return mainButtonLayout;
}

QHBoxLayout * ToolDialog::beginMainButtonLayout( QBoxLayout * attachTo )
{
	if ( !mainButtonLayout )
		mainButtonLayout = addHBoxLayout( attachTo );
	return mainButtonLayout;
}

QHBoxLayout * ToolDialog::beginMainButtonLayout( QGridLayout * attachTo )
{
	if ( !mainButtonLayout )
		mainButtonLayout = addHBoxLayout( attachTo, 0, -1 );
	return mainButtonLayout;
}

QPushButton * ToolDialog::addMainButton( const QString & text, bool isDefaultButton )
{
	if ( mainButtonLayout && mainButtons.count() == 0)
		mainButtonLayout->addStretch( 1 );

	QPushButton * button = addPushButton( mainButtonLayout, text );
	if ( isDefaultButton )
		defaultButton = button;
	mainButtons.append( button );
	return button;
}

QPushButton * ToolDialog::addCloseButton( const QString & text )
{
	QPushButton * button = addMainButton( text, false );
	connect( button, &QPushButton::clicked, this, &QWidget::close );
	return button;
}


// Check box helpers

QCheckBox * ToolDialog::addCheckBox( const QString & text )
{
	return new QCheckBox( text );
}

QCheckBox * ToolDialog::addCheckBox( QBoxLayout * parentLayout, const QString & text )
{
	QCheckBox * checkBox = addCheckBox( text );
	attachWidgetToLayout( checkBox, parentLayout );
	return checkBox;
}

QCheckBox * ToolDialog::addCheckBox( QGridLayout * parentLayout, int layoutColumn, const QString & text )
{
	QCheckBox * checkBox = addCheckBox( text );
	attachWidgetToLayout( checkBox, parentLayout, gridLayoutRow, layoutColumn );
	return checkBox;
}

QCheckBox * ToolDialog::addCheckBox( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, const QString & text )
{
	QCheckBox * checkBox = addCheckBox( text );
	attachWidgetToLayout( checkBox, parentLayout, gridLayoutRow, layoutColumn, layoutSpan );
	return checkBox;
}


// Radio button helpers

QButtonGroup * ToolDialog::beginRadioGroup()
{
	radioGroup = new QButtonGroup( this );
	radioGroup->setExclusive( true );
	return radioGroup;
}

QRadioButton * ToolDialog::addRadioButton( const QString & text, int groupId )
{
	QRadioButton * button = new QRadioButton( text );
	if ( radioGroup )
		radioGroup->addButton( button, groupId );
	return button;
}

QRadioButton * ToolDialog::addRadioButton( QBoxLayout * parentLayout, const QString & text, int groupId )
{
	QRadioButton * button = addRadioButton( text, groupId );
	attachWidgetToLayout( button, parentLayout );
	return button;
}

QRadioButton * ToolDialog::addRadioButton( QGridLayout * parentLayout, int layoutColumn, const QString & text, int groupId )
{
	QRadioButton * button = addRadioButton( text, groupId );
	attachWidgetToLayout( button, parentLayout, gridLayoutRow, layoutColumn );
	return button;
}

QRadioButton * ToolDialog::addRadioButton( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, const QString & text, int groupId )
{
	QRadioButton * button = addRadioButton( text, groupId );
	attachWidgetToLayout( button, parentLayout, gridLayoutRow, layoutColumn, layoutSpan );
	return button;
}


// Group box helpers

QGroupBox * ToolDialog::addGroupBox( const QString & text, QLayout * innerLayout )
{
	auto groupBox = new QGroupBox( text );
	if ( innerLayout )
		groupBox->setLayout( innerLayout );
	return groupBox;
}

QGroupBox * ToolDialog::addGroupBox( QBoxLayout * parentLayout, const QString & text, QLayout * innerLayout )
{
	QGroupBox * groupBox = addGroupBox( text, innerLayout );
	attachWidgetToLayout( groupBox, parentLayout );
	return groupBox;
}

QGroupBox * ToolDialog::addGroupBox( QGridLayout * parentLayout, int layoutColumn, const QString & text, QLayout * innerLayout )
{
	QGroupBox * groupBox = addGroupBox( text, innerLayout );
	attachWidgetToLayout( groupBox, parentLayout, gridLayoutRow, layoutColumn );
	return groupBox;
}

QGroupBox * ToolDialog::addGroupBox( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, const QString & text, QLayout * innerLayout )
{
	QGroupBox * groupBox = addGroupBox( text, innerLayout );
	attachWidgetToLayout( groupBox, parentLayout, gridLayoutRow, layoutColumn, layoutSpan );
	return groupBox;
}


// Spin box helpers

QSpinBox * ToolDialog::addSpinBox( int minVal, int maxVal, int initVal, int step )
{
	auto spinBox = new QSpinBox;
	spinBox->setRange( minVal, maxVal );
	spinBox->setValue( initVal );
	spinBox->setSingleStep( step );

	return spinBox;
}

QSpinBox * ToolDialog::addSpinBox( QBoxLayout * parentLayout, int minVal, int maxVal, int initVal, int step )
{
	QSpinBox * spinBox = addSpinBox( minVal, maxVal, initVal, step );
	attachWidgetToLayout( spinBox, parentLayout );
	return spinBox;
}

QSpinBox * ToolDialog::addSpinBox( QGridLayout * parentLayout, int layoutColumn, int minVal, int maxVal, int initVal, int step )
{
	QSpinBox * spinBox = addSpinBox( minVal, maxVal, initVal, step );
	attachWidgetToLayout( spinBox, parentLayout, gridLayoutRow, layoutColumn );
	return spinBox;
}

QSpinBox * ToolDialog::addSpinBox( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, int minVal, int maxVal, int initVal, int step )
{
	QSpinBox * spinBox = addSpinBox( minVal, maxVal, initVal, step );
	attachWidgetToLayout( spinBox, parentLayout, gridLayoutRow, layoutColumn, layoutSpan );
	return spinBox;
}

QDoubleSpinBox * ToolDialog::addDoubleSpinBox( double minVal, double maxVal, double initVal, int decimalDigits, double step )
{
	auto spinBox = new QDoubleSpinBox;
	spinBox->setDecimals( decimalDigits );
	spinBox->setRange( minVal, maxVal );
	spinBox->setValue( initVal );
	spinBox->setSingleStep( step );

	return spinBox;
}

QDoubleSpinBox * ToolDialog::addDoubleSpinBox( QBoxLayout * parentLayout, double minVal, double maxVal, double initVal, int decimalDigits, double step )
{
	QDoubleSpinBox * spinBox = addDoubleSpinBox( minVal, maxVal, initVal, decimalDigits, step );
	attachWidgetToLayout( spinBox, parentLayout );
	return spinBox;
}

QDoubleSpinBox * ToolDialog::addDoubleSpinBox( QGridLayout * parentLayout, int layoutColumn, double minVal, double maxVal, double initVal, int decimalDigits, double step )
{
	QDoubleSpinBox * spinBox = addDoubleSpinBox( minVal, maxVal, initVal, decimalDigits, step );
	attachWidgetToLayout( spinBox, parentLayout, gridLayoutRow, layoutColumn );
	return spinBox;
}

QDoubleSpinBox * ToolDialog::addDoubleSpinBox( QGridLayout * parentLayout, int layoutColumn, int layoutSpan, double minVal, double maxVal, double initVal, int decimalDigits, double step )
{
	QDoubleSpinBox * spinBox = addDoubleSpinBox( minVal, maxVal, initVal, decimalDigits, step );
	attachWidgetToLayout( spinBox, parentLayout, gridLayoutRow, layoutColumn, layoutSpan );
	return spinBox;
}
