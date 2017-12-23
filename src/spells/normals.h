#ifndef NORMALS_H
#define NORMALS_H

#include "spellbook.h"

// Brief description is deliberately not autolinked to class Spell
/*! \file normals.cpp
 * \brief Vertex normal spells
 *
 * All classes here inherit from the Spell class.
 */

//! Recalculates and faces the normals of a mesh
class spFaceNormals final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Face Normals" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	static QModelIndex getShapeData( const NifModel * nif, const QModelIndex & index );

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

//! Flip normals of a mesh, without recalculating them.
class spFlipNormals final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Flip Normals" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

//! Smooths the normals of a mesh
class spSmoothNormals final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Smooth Normals" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

//! Normalises any single Vector3 or array.
/**
 * Most used on Normals, Bitangents and Tangents.
 */
class spNormalize final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Normalize" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

#endif // NORMALS_H
