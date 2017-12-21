#ifndef BOUNDS_H
#define BOUNDS_H

#include "spellbook.h"

class spEditBounds final : public Spell
{
public:
    QString name() const override final { return Spell::tr( "Edit" ); }
    QString page() const override final { return Spell::tr( "Bounds" ); }

    bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
    QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};

#endif // BOUNDS_H
