#ifndef OPTIMIZE_H
#define OPTIMIZE_H

#include "spellbook.h"

// Brief description is deliberately not autolinked to class Spell
/*! \file optimize.cpp
 * \brief Optimization spells
 *
 * All classes here inherit from the Spell class.
 */

//! Combines properties
/*!
 * This has a tendency to fail due to supposedly boolean values in many NIFs
 * having values apart from 0 and 1.
 *
 * \sa spCombiTris
 * \sa spUniqueProps
 */

class spCombiProps final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Combine Properties" ); }
	QString page() const override final { return Spell::tr( "Optimize" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & ) override final;
};

//! Creates unique properties from shared ones
/*!
 * \sa spDuplicateBlock
 * \sa spCombiProps
 */
class spUniqueProps final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Split Properties" ); }
	QString page() const override final { return Spell::tr( "Optimize" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

//! Removes nodes with no children and singular parents
/*!
 * Note that the user might lose "important" named nodes with this; short of
 * asking for confirmation or simply reporting nodes instead of removing
 * them, there's not much that can be done to prevent a NIF that won't work
 * ingame.
 */
class spRemoveBogusNodes final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Remove Bogus Nodes" ); }
	QString page() const override final { return Spell::tr( "Optimize" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

//! Combines geometry data
/*!
 * Can fail for a number of reasons, usually due to mismatched properties (see
 * spCombiProps for why that can fail) or non-geometry children (extra data,
 * skin instance etc.).
 */
class spCombiTris final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Combine Shapes" ); }
	QString page() const override final { return Spell::tr( "Optimize" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;

	//! Determine if two shapes are identical
	bool matches( const NifModel * nif, const QModelIndex & iTriA, const QModelIndex & iTriB );

	//! Determines if two sets of shape data are identical
	bool dataMatches( const NifModel * nif, const QModelIndex & iDataA, const QModelIndex & iDataB );

	//! Combines meshes a and b ( a += b )
	void combine( NifModel * nif, const QModelIndex & iTriA, const QModelIndex & iTriB );
};

void scan( const QModelIndex & idx, NifModel * nif, QMap<QString, qint32> & usedStrings, bool hasCED );

//! Removes unused strings from the header
class spRemoveUnusedStrings final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Remove Unused Strings" ); }
	QString page() const override final { return Spell::tr( "Optimize" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & ) override final;
};

#endif // OPTIMIZE_H
