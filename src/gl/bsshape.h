#ifndef BSSHAPE_H
#define BSSHAPE_H

#include "gl/glshape.h"


// Nodes of type BSTriShape (FO4/SKSE+)
class BSShape : public Shape
{
public:
	BSShape( Scene * s, const QModelIndex & b ) : Shape( s, b ) { }

	// Node

	void transformShapes() override;

	BoundSphere bounds() const override;

	// end Node

protected:
	BoundSphere dataBound; // TODO: move to Shape, replace with a pointer to BoundSphereSelection?

	bool isDynamic = false;

	void updateDataImpl( const NifModel * nif ) override;
};

#endif // BSSHAPE_H
