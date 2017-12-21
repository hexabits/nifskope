#include "light.h"

QIcon spLightEdit::icon() const
{
	if ( !light42_xpm_icon )
	light42_xpm_icon = QIconPtr( new QIcon(QPixmap( light42_xpm )) );

	return *light42_xpm_icon;
}

bool spLightEdit::isApplicable( const NifModel * nif, const QModelIndex & index )
{
	QModelIndex iBlock  = nif->getBlock( index );
	QModelIndex sibling = index.sibling( index.row(), 0 );
	return index.isValid() && nif->inherits( iBlock, "NiLight" ) && ( iBlock == sibling || nif->getIndex( iBlock, "Name" ) == sibling );
}

QModelIndex spLightEdit::cast( NifModel * nif, const QModelIndex & index )
{
	QModelIndex iLight = nif->getBlock( index );

	NifBlockEditor * le = new NifBlockEditor( nif, iLight );
	le->pushLayout( new QHBoxLayout() );
	le->add( new NifVectorEdit( nif, nif->getIndex( iLight, "Translation" ) ) );
	le->add( new NifRotationEdit( nif, nif->getIndex( iLight, "Rotation" ) ) );
	le->popLayout();
	le->add( new NifFloatSlider( nif, nif->getIndex( iLight, "Dimmer" ), 0, 1.0 ) );
	le->pushLayout( new QHBoxLayout() );
	le->add( new NifColorEdit( nif, nif->getIndex( iLight, "Ambient Color" ) ) );
	le->add( new NifColorEdit( nif, nif->getIndex( iLight, "Diffuse Color" ) ) );
	le->add( new NifColorEdit( nif, nif->getIndex( iLight, "Specular Color" ) ) );
	le->popLayout();
	le->pushLayout( new QHBoxLayout(), "Point Light Parameter" );
	le->add( new NifFloatEdit( nif, nif->getIndex( iLight, "Constant Attenuation" ) ) );
	le->add( new NifFloatEdit( nif, nif->getIndex( iLight, "Linear Attenuation" ) ) );
	le->add( new NifFloatEdit( nif, nif->getIndex( iLight, "Quadratic Attenuation" ) ) );
	le->popLayout();
	le->pushLayout( new QHBoxLayout(), "Spot Light Parameters" );
	le->add( new NifFloatEdit( nif, nif->getIndex( iLight, "Outer Spot Angle" ), 0, 90 ) );
	le->add( new NifFloatEdit( nif, nif->getIndex( iLight, "Inner Spot Angle" ), 0, 90 ) );
	le->add( new NifFloatEdit( nif, nif->getIndex( iLight, "Exponent" ), 0, 128 ) );
	le->popLayout();
	le->show();

	return index;
}

REGISTER_SPELL( spLightEdit )
