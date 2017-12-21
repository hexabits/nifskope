#ifndef COLOR_H
#define COLOR_H

#include "spellbook.h"
#include "ui/widgets/colorwheel.h"

class spChooseColor final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Choose" ); }
	QString page() const override final { return Spell::tr( "Color" ); }
	QIcon icon() const override final { return ColorWheel::getIcon(); }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

class spSetAllColor final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Set All" ); }
	QString page() const override final { return Spell::tr( "Color" ); }
	QIcon icon() const override final { return ColorWheel::getIcon(); }
	bool instant() const override final { return true; }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

#endif // COLOR_H
