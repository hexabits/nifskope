#include "color.h"




// Brief description is deliberately not autolinked to class Spell
/*! \file color.cpp
* \brief Color editing spells (spChooseColor)
*
* All classes here inherit from the Spell class.
*/


bool spChooseColor::isApplicable( const NifModel * nif, const QModelIndex & index )
{
	return nif->getValue( index ).isColor();
}

QModelIndex spChooseColor::cast( NifModel * nif, const QModelIndex & index )
{
	auto typ = nif->getValue( index ).type();
	if ( typ == NifValue::tColor3 ) {
		nif->set<Color3>( index, ColorWheel::choose( nif->get<Color3>( index ) ) );
	} else if ( typ == NifValue::tColor4 ) {
		nif->set<Color4>( index, ColorWheel::choose( nif->get<Color4>( index ) ) );
	} else if ( typ == NifValue::tByteColor4 ) {
		auto col = ColorWheel::choose( nif->get<ByteColor4>( index ) );
		nif->set<ByteColor4>( index, *static_cast<ByteColor4 *>(&col) );
	}


	return index;
}

REGISTER_SPELL( spChooseColor )

bool spSetAllColor::isApplicable( const NifModel * nif, const QModelIndex & index )
{
	return nif->isArray( index ) && nif->getValue( index.child( 0, 0 ) ).isColor();
}

QModelIndex spSetAllColor::cast( NifModel * nif, const QModelIndex & index )
{
	QModelIndex colorIdx = (nif->isArray( index )) ? index.child( 0, 0 ) : index;

	auto typ = nif->getValue( colorIdx ).type();
	if ( typ == NifValue::tColor3 )
	nif->setArray<Color3>( index, ColorWheel::choose( nif->get<Color3>( colorIdx ) ) );
	else if ( typ == NifValue::tColor4 )
	nif->setArray<Color4>( index, ColorWheel::choose( nif->get<Color4>( colorIdx ) ) );

	return index;
}

REGISTER_SPELL( spSetAllColor )
