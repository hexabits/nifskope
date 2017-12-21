#include "headerstring.h"


QIcon spEditStringIndex::icon() const
{
	if ( !txt_xpm_icon )
	txt_xpm_icon = QIconPtr( new QIcon(QPixmap( txt_xpm )) );

	return *txt_xpm_icon;
}

bool spEditStringIndex::isApplicable( const NifModel * nif, const QModelIndex & index )
{
	NifValue::Type type = nif->getValue( index ).type();

	if ( type == NifValue::tStringIndex )
	return true;

	if ( (type == NifValue::tString || type == NifValue::tFilePath) && nif->checkVersion( 0x14010003, 0 ) )
	return true;

	return false;
}

QModelIndex spEditStringIndex::cast( NifModel * nif, const QModelIndex & index )
{
	int offset = nif->get<int>( index );
	QStringList strings;
	QString string;

	if ( nif->getValue( index ).type() != NifValue::tStringIndex || !nif->checkVersion( 0x14010003, 0 ) )
	return index;

	QModelIndex header = nif->getHeader();
	QVector<QString> stringVector = nif->getArray<QString>( header, "Strings" );
	strings = stringVector.toList();

	if ( offset >= 0 && offset < stringVector.size() )
	string = stringVector.at( offset );

	QDialog dlg;

	QLabel * lb = new QLabel( &dlg );
	lb->setText( Spell::tr( "Select a string or enter a new one" ) );

	QListWidget * lw = new QListWidget( &dlg );
	lw->addItems( strings );

	QLineEdit * le = new QLineEdit( &dlg );
	le->setText( string );
	le->setFocus();

	QObject::connect( lw, &QListWidget::currentTextChanged, le, &QLineEdit::setText );
	QObject::connect( lw, &QListWidget::itemActivated, &dlg, &QDialog::accept );
	QObject::connect( le, &QLineEdit::returnPressed, &dlg, &QDialog::accept );

	QPushButton * bo = new QPushButton( Spell::tr( "Ok" ), &dlg );
	QObject::connect( bo, &QPushButton::clicked, &dlg, &QDialog::accept );

	QPushButton * bc = new QPushButton( Spell::tr( "Cancel" ), &dlg );
	QObject::connect( bc, &QPushButton::clicked, &dlg, &QDialog::reject );

	QGridLayout * grid = new QGridLayout;
	dlg.setLayout( grid );
	grid->addWidget( lb, 0, 0, 1, 2 );
	grid->addWidget( lw, 1, 0, 1, 2 );
	grid->addWidget( le, 2, 0, 1, 2 );
	grid->addWidget( bo, 3, 0, 1, 1 );
	grid->addWidget( bc, 3, 1, 1, 1 );

	if ( dlg.exec() != QDialog::Accepted )
	return index;

	nif->set<QString>( index, le->text() );

	return index;
}

REGISTER_SPELL( spEditStringIndex )
