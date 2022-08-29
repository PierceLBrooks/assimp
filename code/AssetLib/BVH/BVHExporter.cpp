/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

Copyright (c) 2006-2022, assimp team


All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the
following conditions are met:

* Redistributions of source code must retain the above
copyright notice, this list of conditions and the
following disclaimer.

* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the
following disclaimer in the documentation and/or other
materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
contributors may be used to endorse or promote products
derived from this software without specific prior
written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

----------------------------------------------------------------------
*/
#ifndef ASSIMP_BUILD_NO_EXPORT
#ifndef ASSIMP_BUILD_NO_BVH_EXPORTER

#include "AssetLib/BVH/BVHExporter.h"

#include <assimp/commonMetaData.h>
#include <assimp/Exceptional.h>
#include <assimp/StringComparison.h>
#include <assimp/ByteSwapper.h>
#include <assimp/SceneCombiner.h>
#include <assimp/version.h>
#include <assimp/IOSystem.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>

// Header files, standard library.
#include <memory>
#include <limits>
#include <inttypes.h>

using namespace Assimp;

namespace Assimp {

    // ------------------------------------------------------------------------------------------------
    // Worker function for exporting a scene to BVH. Prototyped and registered in Exporter.cpp
    void ExportSceneBVH(const char* pFile, IOSystem* pIOSystem, const aiScene* pScene, const ExportProperties* pProperties)
    {
        // invoke the exporter
        BVHExporter exporter(pFile, pIOSystem, pScene, pProperties);
    }

} // end of namespace Assimp

BVHExporter::BVHExporter(const char* filename, IOSystem* pIOSystem, const aiScene* pScene,
                           const ExportProperties* pProperties)
    : mFilename(filename)
    , mIOSystem(pIOSystem)
    , mProperties(pProperties)
{
    mAsset.reset( new BVH::Asset( pIOSystem ) );

    ExportMeshes();

    writer.WriteFile(filename);
}

/*
 * Search through node hierarchy and find the node containing the given meshID.
 * Returns true on success, and false otherwise.
 */
bool FindMeshNode(Ref<Node> &nodeIn, Ref<Node> &meshNode, const std::string &meshID) {
    for (unsigned int i = 0; i < nodeIn->meshes.size(); ++i) {
        if (meshID.compare(nodeIn->meshes[i]->id) == 0) {
          meshNode = nodeIn;
          return true;
        }
    }

    for (unsigned int i = 0; i < nodeIn->children.size(); ++i) {
        if(FindMeshNode(nodeIn->children[i], meshNode, meshID)) {
          return true;
        }
    }

    return false;
}

/*
 * Find the root joint of the skeleton.
 * Starts will any joint node and traces up the tree,
 * until a parent is found that does not have a jointName.
 * Returns the first parent Ref<Node> found that does not have a jointName.
 */
Ref<Node> FindSkeletonRootJoint(Ref<Skin>& skinRef)
{
    Ref<Node> startNodeRef;
    Ref<Node> parentNodeRef;

    // Arbitrarily use the first joint to start the search.
    startNodeRef = skinRef->jointNames[0];
    parentNodeRef = skinRef->jointNames[0];

    do {
        startNodeRef = parentNodeRef;
        parentNodeRef = startNodeRef->parent;
    } while (!parentNodeRef->jointName.empty());

    return parentNodeRef;
}

void ExportSkin(Asset& mAsset, const aiMesh* aimesh, Ref<Mesh>& meshRef, Ref<Buffer>& bufferRef, Ref<Skin>& skinRef, std::vector<aiMatrix4x4>& inverseBindMatricesData)
{
    if (aimesh->mNumBones < 1) {
        return;
    }

    // Store the vertex joint and weight data.
    const size_t NumVerts( aimesh->mNumVertices );
    vec4* vertexJointData = new vec4[ NumVerts ];
    vec4* vertexWeightData = new vec4[ NumVerts ];
    int* jointsPerVertex = new int[ NumVerts ];
    for (size_t i = 0; i < NumVerts; ++i) {
        jointsPerVertex[i] = 0;
        for (size_t j = 0; j < 4; ++j) {
            vertexJointData[i][j] = 0;
            vertexWeightData[i][j] = 0;
        }
    }

    for (unsigned int idx_bone = 0; idx_bone < aimesh->mNumBones; ++idx_bone) {
        const aiBone* aib = aimesh->mBones[idx_bone];

        // aib->mName   =====>  skinRef->jointNames
        // Find the node with id = mName.
        Ref<Node> nodeRef = mAsset.nodes.Get(aib->mName.C_Str());
        nodeRef->jointName = nodeRef->id;

        unsigned int jointNamesIndex = 0;
        bool addJointToJointNames = true;
        for ( unsigned int idx_joint = 0; idx_joint < skinRef->jointNames.size(); ++idx_joint) {
            if (skinRef->jointNames[idx_joint]->jointName.compare(nodeRef->jointName) == 0) {
                addJointToJointNames = false;
                jointNamesIndex = idx_joint;
            }
        }

        if (addJointToJointNames) {
            skinRef->jointNames.push_back(nodeRef);

            // aib->mOffsetMatrix   =====>  skinRef->inverseBindMatrices
            aiMatrix4x4 tmpMatrix4;
            CopyValue(aib->mOffsetMatrix, tmpMatrix4);
            inverseBindMatricesData.push_back(tmpMatrix4);
            jointNamesIndex = static_cast<unsigned int>(inverseBindMatricesData.size() - 1);
        }

        // aib->mWeights   =====>  vertexWeightData
        for (unsigned int idx_weights = 0; idx_weights < aib->mNumWeights; ++idx_weights) {
            unsigned int vertexId = aib->mWeights[idx_weights].mVertexId;
            float vertWeight      = aib->mWeights[idx_weights].mWeight;

            // A vertex can only have at most four joint weights. Ignore all others.
            if (jointsPerVertex[vertexId] > 3) {
                continue;
            }

            vertexJointData[vertexId][jointsPerVertex[vertexId]] = static_cast<float>(jointNamesIndex);
            vertexWeightData[vertexId][jointsPerVertex[vertexId]] = vertWeight;

            jointsPerVertex[vertexId] += 1;
        }

    } // End: for-loop mNumMeshes

    Mesh::Primitive& p = meshRef->primitives.back();
    Ref<Accessor> vertexJointAccessor = ExportData(mAsset, skinRef->id, bufferRef, aimesh->mNumVertices, vertexJointData, AttribType::VEC4, AttribType::VEC4, ComponentType_FLOAT);
    if ( vertexJointAccessor ) {
        p.attributes.joint.push_back( vertexJointAccessor );
    }

    Ref<Accessor> vertexWeightAccessor = ExportData(mAsset, skinRef->id, bufferRef, aimesh->mNumVertices, vertexWeightData, AttribType::VEC4, AttribType::VEC4, ComponentType_FLOAT);
    if ( vertexWeightAccessor ) {
        p.attributes.weight.push_back( vertexWeightAccessor );
    }
    delete[] jointsPerVertex;
    delete[] vertexWeightData;
    delete[] vertexJointData;
}

void BVHExporter::ExportMeshes()
{
    // Not for
    //     using IndicesType = decltype(aiFace::mNumIndices);
    // But yes for
    //     using IndicesType = unsigned short;
    // because "ComponentType_UNSIGNED_SHORT" used for indices. And it's a maximal type according to BVH specification.
    typedef unsigned short IndicesType;

    std::string fname = std::string(mFilename);
    std::string bufferIdPrefix = fname.substr(0, fname.rfind(".bvh"));
    std::string bufferId = mAsset->FindUniqueID("", bufferIdPrefix.c_str());

    Ref<Buffer> b = mAsset->GetBodyBuffer();
    if (!b) {
       b = mAsset->buffers.Create(bufferId);
    }

    //----------------------------------------
    // Initialize variables for the skin
    bool createSkin = false;
    for (unsigned int idx_mesh = 0; idx_mesh < mScene->mNumMeshes; ++idx_mesh) {
        const aiMesh* aim = mScene->mMeshes[idx_mesh];
        if(aim->HasBones()) {
            createSkin = true;
            break;
        }
    }

    Ref<Skin> skinRef;
    std::string skinName = mAsset->FindUniqueID("skin", "skin");
    std::vector<aiMatrix4x4> inverseBindMatricesData;
    if(createSkin) {
        skinRef = mAsset->skins.Create(skinName);
        skinRef->name = skinName;
    }
    //----------------------------------------

	for (unsigned int idx_mesh = 0; idx_mesh < mScene->mNumMeshes; ++idx_mesh) {
		const aiMesh* aim = mScene->mMeshes[idx_mesh];

        std::string meshId = mAsset->FindUniqueID(aim->mName.C_Str(), "mesh");
        Ref<Mesh> m = mAsset->meshes.Create(meshId);
        
        /*************** Skins ****************/
        if(aim->HasBones()) {
            ExportSkin(*mAsset, aim, m, b, skinRef, inverseBindMatricesData);
        }

	}// for (unsigned int i = 0; i < mScene->mNumMeshes; ++i)

    //----------------------------------------
    // Finish the skin
    // Create the Accessor for skinRef->inverseBindMatrices
    if (createSkin) {
        mat4* invBindMatrixData = new mat4[inverseBindMatricesData.size()];
        for ( unsigned int idx_joint = 0; idx_joint < inverseBindMatricesData.size(); ++idx_joint) {
            CopyValue(inverseBindMatricesData[idx_joint], invBindMatrixData[idx_joint]);
        }

        Ref<Accessor> invBindMatrixAccessor = ExportData(*mAsset, skinName, b, static_cast<unsigned int>(inverseBindMatricesData.size()), invBindMatrixData, AttribType::MAT4, AttribType::MAT4, ComponentType_FLOAT);
        if (invBindMatrixAccessor) skinRef->inverseBindMatrices = invBindMatrixAccessor;

        // Identity Matrix   =====>  skinRef->bindShapeMatrix
        // Temporary. Hard-coded identity matrix here
        skinRef->bindShapeMatrix.isPresent = true;
        IdentityMatrix4(skinRef->bindShapeMatrix.value);

        // Find node that contains this mesh and add "skeletons" and "skin" attributes to that node.
        Ref<Node> rootNode = mAsset->nodes.Get(unsigned(0));
        Ref<Node> meshNode;
        std::string meshID = mAsset->meshes.Get(unsigned(0))->id;
        FindMeshNode(rootNode, meshNode, meshID);

        Ref<Node> rootJoint = FindSkeletonRootJoint(skinRef);
        meshNode->skeletons.push_back(rootJoint);
        meshNode->skin = skinRef;
    }
}


#endif // ASSIMP_BUILD_NO_BVH_EXPORTER
#endif // ASSIMP_BUILD_NO_EXPORT
