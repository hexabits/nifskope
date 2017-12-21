#include "materialedit.h"

QIcon spMaterialEdit::icon() const
{
	if ( !mat42_xpm_icon )
	mat42_xpm_icon = QIconPtr( new QIcon(QPixmap( mat42_xpm )) );

	return *mat42_xpm_icon;
}

bool spMaterialEdit::isApplicable( const NifModel * nif, const QModelIndex & index )
{
	QModelIndex iBlock  = nif->getBlock( index, "NiMaterialProperty" );
	QModelIndex sibling = index.sibling( index.row(), 0 );
	return index.isValid() && ( iBlock == sibling || nif->getIndex( iBlock, "Name" ) == sibling );
}

QModelIndex spMaterialEdit::cast( NifModel * nif, const QModelIndex & index )
{
	QModelIndex iMaterial = nif->getBlock( index );
	NifBlockEditor * me = new NifBlockEditor( nif, iMaterial );

	me->pushLayout( new QHBoxLayout );
	me->add( new NifColorEdit( nif, nif->getIndex( iMaterial, "Ambient Color" ) ) );
	me->add( new NifColorEdit( nif, nif->getIndex( iMaterial, "Diffuse Color" ) ) );
	me->popLayout();
	me->pushLayout( new QHBoxLayout );
	me->add( new NifColorEdit( nif, nif->getIndex( iMaterial, "Specular Color" ) ) );
	me->add( new NifColorEdit( nif, nif->getIndex( iMaterial, "Emissive Color" ) ) );
	me->popLayout();
	me->add( new NifFloatSlider( nif, nif->getIndex( iMaterial, "Alpha" ), 0.0, 1.0 ) );
	me->add( new NifFloatSlider( nif, nif->getIndex( iMaterial, "Glossiness" ), 0.0, 100.0 ) );
	me->setWindowModality( Qt::ApplicationModal );
	me->show();

	return index;
}


REGISTER_SPELL( spMaterialEdit )
