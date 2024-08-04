#pragma once
#include "glshape.h"
// #include "io/MeshFile.h"

#include <memory>
#include <functional>

#include <QString>

class QByteArray;
class NifModel;
class MeshFile;

namespace tinygltf {
	class Model;
}

//! A bone, weight pair
class BoneWeightUNORM16 final
{
public:
	BoneWeightUNORM16()
		: bone( 0 ), weight( 0.0f ) {}
	BoneWeightUNORM16(quint16 b, float w)
		: bone( b ), weight( w ) {}

	quint16 bone;
	float weight;
};

class BoneWeightsUNorm : public SkinBone
{
public:
	BoneWeightsUNorm() {}
	BoneWeightsUNorm(QVector<QPair<quint16, quint16>> weights);

	QVector<BoneWeightUNORM16> weightsUNORM;
};

class BSMesh : public Shape
{
public:
	BSMesh( Scene * _scene, NifFieldConst _block );

	// Node

	void transformShapes() override;

	void drawShapes(NodeList* secondPass = nullptr, bool presort = false) override;
	void drawSelection() const override;

	BoundSphere bounds() const override;

	QString textStats() const override; // TODO (Gavrant): move to Shape

	void forMeshIndex(const NifModel* nif, std::function<void (const QString&, int)>& f);
	int meshCount();

	// end Node

	QVector<std::shared_ptr<MeshFile>> meshes;

	int materialID = 0;
	QString materialPath;

	int skinID = -1;
	QVector<BoneWeightsUNorm> weightsUNORM;
	QVector<QVector<Triangle>> gpuLODs;
	QVector<QString> boneNames;
	QVector<Transform> boneTransforms;

protected:
	void updateImpl(const NifModel* nif, const QModelIndex& index) override;
	void updateDataImpl() override;

	QModelIndex iMeshes;

	BoundSphere dataBound;

	quint32 lodLevel = 0;
};
