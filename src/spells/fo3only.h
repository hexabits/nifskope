#ifndef FO3ONLY_H
#define FO3ONLY_H

#include "spellbook.h"


// Brief description is deliberately not autolinked to class Spell
/*! \file fo3only.cpp
* \brief Fallout 3 specific spells (spFO3FixShapeDataName)
*
* All classes here inherit from the Spell class.
*/

//! Set the name of the NiGeometryData node to parent name or zero
class spFO3FixShapeDataName final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Fix Geometry Data Names" ); }
	QString page() const override final { return Spell::tr( "Sanitize" ); }
	bool sanity() const override final { return true; }

	//////////////////////////////////////////////////////////////////////////
	// Valid if nothing or NiGeometryData-based node is selected
	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

#endif // FO3ONLY_H
