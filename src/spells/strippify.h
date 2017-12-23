#ifndef STRIPPIFY_H
#define STRIPPIFY_H

#include "spellbook.h"

class spStrippify final : public Spell
{
	QString name() const override final { return Spell::tr( "Stripify" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

class spStrippifyAll final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Stripify all TriShapes" ); }
	QString page() const override final { return Spell::tr( "Optimize" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & ) override final;

};

class spTriangulate final : public Spell
{
	QString name() const override final { return Spell::tr( "Triangulate" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

class spTriangulateAll final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Triangulate All Strips" ); }
	QString page() const override final { return Spell::tr( "Batch" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & ) override final;
};

class spStichStrips final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Stich Strips" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	static QModelIndex getStripsData( const NifModel * nif, const QModelIndex & index );

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

class spUnstichStrips final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Unstich Strips" ); }
	QString page() const override final { return Spell::tr( "Mesh" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

#endif // STRIPPIFY_H
