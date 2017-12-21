#ifndef FLAGS_H
#define FLAGS_H

#include "spellbook.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLayout>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

//! Node / Property types on which flags are applicable
enum FlagType
{
	Alpha,
	Billboard,
	Controller,
	MatColControl,
	Node,
	RigidBody,
	Shape,
	Stencil,
	TexDesc,
	VertexColor,
	ZBuffer,
	BSX,
	NiAVObject,
	None
};

//! Edit flags
class spEditFlags : public Spell
{
public:
	QString name() const override { return Spell::tr( "Flags" ); }
	bool instant() const override { return true; }
	QIcon icon() const override { return QIcon( ":/img/flag" ); }

	//! Find the index of flags relative to a given NIF index
	QModelIndex getFlagIndex( const NifModel * nif, const QModelIndex & index ) const;

	//! Determine the applicable flag editing dialog for a NIF block type
	FlagType queryType( const NifModel * nif, const QModelIndex & index ) const;

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override;
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override;

	void alphaFlags( NifModel * nif, const QModelIndex & index );
	void nodeFlags( NifModel * nif, const QModelIndex & index );
	void controllerFlags( NifModel * nif, const QModelIndex & index );
	void bodyFlags( NifModel * nif, const QModelIndex & index );
	void shapeFlags( NifModel * nif, const QModelIndex & index );
	void zbufferFlags( NifModel * nif, const QModelIndex & index );
	void bsxFlags( NifModel * nif, const QModelIndex & index );
	void niavFlags( NifModel * nif, const QModelIndex & index );
	void billboardFlags( NifModel * nif, const QModelIndex & index );
	void stencilFlags( NifModel * nif, const QModelIndex & index );
	void vertexColorFlags( NifModel * nif, const QModelIndex & index );
	void matColControllerFlags( NifModel * nif, const QModelIndex & index );
	void texDescFlags( NifModel * nif, const QModelIndex & index );
	QCheckBox * dlgCheck( QVBoxLayout * vbox, const QString & name, QCheckBox * chk = nullptr );
	QComboBox * dlgCombo( QVBoxLayout * vbox, const QString & name, QStringList items, QCheckBox * chk = nullptr );
	QSpinBox * dlgSpin( QVBoxLayout * vbox, const QString & name, int min, int max, QCheckBox * chk = nullptr );
	void dlgButtons( QDialog * dlg, QVBoxLayout * vbox );
};

class spEditVertexDesc final : public spEditFlags
{
public:
	QString name() const override final { return Spell::tr( "Vertex Flags" ); }
	bool instant() const override final { return true; }
	QIcon icon() const override final { return QIcon( ":/img/flag" ); }

	bool isApplicable( const NifModel * nif, const QModelIndex & index ) override final;
	QModelIndex cast( NifModel * nif, const QModelIndex & index ) override final;

};

#endif // FLAGS_H
