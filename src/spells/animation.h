#ifndef ANIMATION_H
#define ANIMATION_H

#include "spellbook.h"

class spAttachKf final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Attach .KF" ); }
	QString page() const override final { return Spell::tr( "Animation" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final
	{
		return nif && !index.isValid();
	}

	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
	static QModelIndex findChildNode( const NifModel * nif, const QModelIndex & parent, const QString & name );
	static QModelIndex findRootTarget( const NifModel * nif, const QString & name );
	static QModelIndex findController( const NifModel * nif, const QModelIndex & node, const QString & ctrltype );
	static QModelIndex attachController( NifModel * nif, const QPersistentModelIndex & iNode, const QString & ctrltype, bool fast = false );
	static void setLinkArray( NifModel * nif, const QModelIndex & iParent, const QString & array, const QList<QPersistentModelIndex> & iBlocks );
	static void setNameLinkArray( NifModel * nif, const QModelIndex & iParent, const QString & array, const QList<QPersistentModelIndex> & iBlocks );
};

class spConvertQuatsToEulers final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Convert Quat- to ZYX-Rotations" ); }
	QString page() const override final { return Spell::tr( "Animation" ); }
	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
	//QModelIndex cast( NifModel * nif, const QModelIndex & index );
};

class spFixAVObjectPalette final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Fix Invalid AV Object Refs" ); }
	QString page() const override final { return Spell::tr( "Animation" ); }
	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;
};
#endif // ANIMATION_H
