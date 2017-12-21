#ifndef HAVOK_H
#define HAVOK_H

#include "spellbook.h"

#include "spells/blocks.h"

#include "lib/nvtristripwrapper.h"
#include "lib/qhull.h"

#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLayout>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>

#include <algorithm> // std::sort

class spCreateCVS final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Create Convex Shape" ); }
	QString page() const override final { return Spell::tr( "Havok" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

//! Transforms Havok constraints
class spConstraintHelper final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "A -> B" ); }
	QString page() const override final { return Spell::tr( "Havok" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
	static Transform bodyTrans( const NifModel * nif, const QModelIndex & index );
};

//! Calculates Havok spring lengths
class spStiffSpringHelper final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Calculate Spring Length" ); }
	QString page() const override final { return Spell::tr( "Havok" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & idx ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & idx ) override final;
};

//! Packs Havok strips
class spPackHavokStrips final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Pack Strips" ); }
	QString page() const override final { return Spell::tr( "Havok" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & idx ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & iBlock ) override final;
};

#endif // HAVOK_H
