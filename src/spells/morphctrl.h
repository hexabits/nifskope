#ifndef MORPHCTRL_H
#define MORPHCTRL_H

#include "spellbook.h"

class spMorphFrameSave final : public Spell
{
public:
	QString name() const override final { return Spell::tr( "Save Vertices To Frame" ); }
	QString page() const override final { return Spell::tr( "Morph" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;

	//! Helper function to get the Mesh data
	QModelIndex getMeshData( const NifModel * nif, const QModelIndex & iMorpher );

	//! Helper function to get the morph data
	QModelIndex getMorphData( const NifModel * nif, const QModelIndex & iMorpher );

	//! Helper function to get the morph frame array
	QModelIndex getFrameArray( const NifModel * nif, const QModelIndex & iMorpher );

	//! Helper function to get the list of morph frames
	QStringList listFrames( const NifModel * nif, const QModelIndex & iMorpher );
};

#endif // MORPHCTRL_H
