#ifndef NVTRISTRIP_WRAPPER_H
#define NVTRISTRIP_WRAPPER_H

#include <QList>
#include <QVector>
#include "data/niftypes.h"

QVector<TriStrip> stripifyTriangles( const QVector<Triangle> & triangles, bool stitch = true );
QVector<Triangle> triangulateStrip( const TriStrip & stripPoints );
QVector<Triangle> triangulateStrips( const NifModel * nif, const QModelIndex & iStrips );

#endif
