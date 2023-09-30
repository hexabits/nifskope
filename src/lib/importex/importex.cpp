/***** BEGIN LICENSE BLOCK *****

BSD License

Copyright (c) 2005-2015, NIF File Format Library and Tools
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the NIF File Format Library and Tools project may not be
   used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

***** END LICENCE BLOCK *****/

#include "nifskope.h"
#include "gl/glscene.h"
#include "glview.h"
#include "model/nifmodel.h"
#include "model/nifproxymodel.h"
#include "ui/widgets/nifview.h"

#include <QDockWidget>
#include <QFileInfo>
#include <QMenu>
#include <QModelIndex>

#include <string>
#include <functional>


void exportObj( const NifModel * nif, const Scene* scene, const QModelIndex & index );
void exportCol( const NifModel * nif, const Scene* scene, QFileInfo );
void importObjMain( NifModel * nif, const QModelIndex & index, bool collision = false );
void importObj( NifModel* nif, const QModelIndex& index );
void importObjAsCollision( NifModel * nif, const QModelIndex & index );
void import3ds( NifModel * nif, const QModelIndex & index );

void exportGltf(const NifModel* nif, const Scene* scene, const QModelIndex& index);


struct ImportExportOption
{
	std::string name;
	std::function<void(NifModel*, const QModelIndex&)> importFn;
	std::function<void(const NifModel*, const Scene*, const QModelIndex&)> exportFn;
	quint32 minBSVersion = 0;
	quint32 maxBSVersion = 0;
	quint32 minVersion = 0;
	quint32 maxVersion = 0x14050000 - 1; // < 0x14050000 (20.5)
};


const QVector<ImportExportOption> impexOptions{
	ImportExportOption{ ".OBJ", importObj, exportObj, 0, 172 - 1 },
	ImportExportOption{ ".OBJ as Collision", importObjAsCollision, nullptr, 1, 172 - 1 },
	ImportExportOption{ ".glTF", nullptr, exportGltf, 172 },
};


void NifSkope::fillImportExportMenus()
{
	int impexIndex = 0;
	for ( const auto& option : impexOptions ) {
		if ( option.exportFn ) {
			auto a = mExport->addAction(tr("Export %1").arg(option.name.c_str()));
			a->setData(impexIndex);
		}
		if ( option.importFn ) {
			auto a = mImport->addAction(tr("Import %1").arg(option.name.c_str()));
			a->setData(impexIndex);
		}
		impexIndex++;
	}
}

void NifSkope::updateImportExportMenu(const QMenu * menu)
{
	for ( const auto a : menu->actions() ) {
		a->setEnabled(false);
		auto impex = impexOptions.value(a->data().toInt(), {});
		if ( impex.importFn || impex.exportFn ) {
			if ( BaseModel::checkVersion(nif->getVersionNumber(), impex.minVersion, impex.maxVersion)
				&& BaseModel::checkVersion(nif->getBSVersion(), impex.minBSVersion, impex.maxBSVersion) )
			{
				a->setEnabled(true);
			}
		}
	}
}

void NifSkope::sltImport( QAction* a )
{
	// Do not require index.isValid(), let fn deal with it
	QModelIndex index = currentNifIndex();
	auto impex = impexOptions.value(a->data().toInt(), {});
	if ( impex.importFn ) { 
		impex.importFn(nif, index);
	}
}

void NifSkope::sltExport( QAction* a )
{
	// Do not require index.isValid(), let fn deal with it
	QModelIndex index = currentNifIndex();
	auto impex = impexOptions.value(a->data().toInt(), {});
	if ( impex.exportFn ) {
		impex.exportFn(nif, ogl->scene, index);
	}
}