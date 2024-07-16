#ifndef BSSHAPE_H
#define BSSHAPE_H

#include "gl/glshape.h"


class BSShape : public Shape
{
public:
	BSShape( Scene * s, const QModelIndex & b ) : Shape( s, b ) { }

	// Node

	void transformShapes() override;

	BoundSphere bounds() const override;

	// end Node

	// Shape

	QModelIndex vertexAt( int ) const override;

protected:
	BoundSphere dataBound; // TODO: move to Shape, replace with a pointer to BoundSphereSelection (togetger with bounds() )

	bool isDynamic = false;

	void updateDataImpl( const NifModel * nif ) override;
};

#endif // BSSHAPE_H
