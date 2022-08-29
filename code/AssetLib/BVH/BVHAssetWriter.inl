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

#include <assimp/Base64.hpp>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>

#if _MSC_VER
#    pragma warning(push)
#    pragma warning( disable : 4706)
#endif // _MSC_VER

namespace BVH {

    namespace {

        template<typename T, size_t N>
        inline
        Value& MakeValue(Value& val, T(&r)[N], MemoryPoolAllocator<>& al) {
            val.SetArray();
            val.Reserve(N, al);
            for (decltype(N) i = 0; i < N; ++i) {
                val.PushBack(r[i], al);
            }
            return val;
        }

        template<typename T>
        inline
        Value& MakeValue(Value& val, const std::vector<T> & r, MemoryPoolAllocator<>& al) {
            val.SetArray();
            val.Reserve(static_cast<rapidjson::SizeType>(r.size()), al);
            for (unsigned int i = 0; i < r.size(); ++i) {
                val.PushBack(r[i], al);
            }
            return val;
        }

        template<typename C, typename T>
        inline Value& MakeValueCast(Value& val, const std::vector<T> & r, MemoryPoolAllocator<>& al) {
            val.SetArray();
            val.Reserve(static_cast<rapidjson::SizeType>(r.size()), al);
            for (unsigned int i = 0; i < r.size(); ++i) {
                val.PushBack(static_cast<C>(r[i]), al);
            }
            return val;
        }

        template<class T>
        inline void AddRefsVector(Value& obj, const char* fieldId, std::vector< Ref<T> >& v, MemoryPoolAllocator<>& al) {
            if (v.empty()) return;
            Value lst;
            lst.SetArray();
            lst.Reserve(unsigned(v.size()), al);
            for (size_t i = 0; i < v.size(); ++i) {
                lst.PushBack(StringRef(v[i]->id), al);
            }
            obj.AddMember(StringRef(fieldId), lst, al);
        }


    }

    inline void Write(Value& obj, Accessor& a, AssetWriter& w)
    {
        obj.AddMember("bufferView", Value(a.bufferView->id, w.mAl).Move(), w.mAl);
        obj.AddMember("byteOffset", a.byteOffset, w.mAl);
        obj.AddMember("byteStride", a.byteStride, w.mAl);
        obj.AddMember("componentType", int(a.componentType), w.mAl);
        obj.AddMember("count", a.count, w.mAl);
        obj.AddMember("type", StringRef(AttribType::ToString(a.type)), w.mAl);

        Value vTmpMax, vTmpMin;
		if (a.componentType == ComponentType_FLOAT) {
			obj.AddMember("max", MakeValue(vTmpMax, a.max, w.mAl), w.mAl);
			obj.AddMember("min", MakeValue(vTmpMin, a.min, w.mAl), w.mAl);
		} else {
			obj.AddMember("max", MakeValueCast<int64_t>(vTmpMax, a.max, w.mAl), w.mAl);
			obj.AddMember("min", MakeValueCast<int64_t>(vTmpMin, a.min, w.mAl), w.mAl);
		}
    }

    inline void Write(Value& obj, Animation& a, AssetWriter& w)
    {
        /****************** Channels *******************/
        Value channels;
        channels.SetArray();
        channels.Reserve(unsigned(a.Channels.size()), w.mAl);

        for (size_t i = 0; i < unsigned(a.Channels.size()); ++i) {
            Animation::AnimChannel& c = a.Channels[i];
            Value valChannel;
            valChannel.SetObject();
            {
                valChannel.AddMember("sampler", c.sampler, w.mAl);

                Value valTarget;
                valTarget.SetObject();
                {
                    valTarget.AddMember("id", StringRef(c.target.id->id), w.mAl);
                    valTarget.AddMember("path", c.target.path, w.mAl);
                }
                valChannel.AddMember("target", valTarget, w.mAl);
            }
            channels.PushBack(valChannel, w.mAl);
        }
        obj.AddMember("channels", channels, w.mAl);

        /****************** Parameters *******************/
        Value valParameters;
        valParameters.SetObject();
        {
            if (a.Parameters.TIME) {
                valParameters.AddMember("TIME", StringRef(a.Parameters.TIME->id), w.mAl);
            }
            if (a.Parameters.rotation) {
                valParameters.AddMember("rotation", StringRef(a.Parameters.rotation->id), w.mAl);
            }
            if (a.Parameters.scale) {
                valParameters.AddMember("scale", StringRef(a.Parameters.scale->id), w.mAl);
            }
            if (a.Parameters.translation) {
                valParameters.AddMember("translation", StringRef(a.Parameters.translation->id), w.mAl);
            }
        }
        obj.AddMember("parameters", valParameters, w.mAl);

        /****************** Samplers *******************/
        Value valSamplers;
        valSamplers.SetObject();

        for (size_t i = 0; i < unsigned(a.Samplers.size()); ++i) {
            Animation::AnimSampler& s = a.Samplers[i];
            Value valSampler;
            valSampler.SetObject();
            {
                valSampler.AddMember("input", s.input, w.mAl);
                valSampler.AddMember("interpolation", s.interpolation, w.mAl);
                valSampler.AddMember("output", s.output, w.mAl);
            }
            valSamplers.AddMember(StringRef(s.id), valSampler, w.mAl);
        }
        obj.AddMember("samplers", valSamplers, w.mAl);
    }

    inline void Write(Value& obj, Buffer& b, AssetWriter& w)
    {
        const char* type;
        switch (b.type) {
            case Buffer::Type_text:
                type = "text"; break;
            default:
                type = "arraybuffer";
        }

        obj.AddMember("byteLength", static_cast<uint64_t>(b.byteLength), w.mAl);
        obj.AddMember("type", StringRef(type), w.mAl);
        obj.AddMember("uri", Value(b.GetURI(), w.mAl).Move(), w.mAl);
    }

    inline void Write(Value& obj, BufferView& bv, AssetWriter& w)
    {
        obj.AddMember("buffer", Value(bv.buffer->id, w.mAl).Move(), w.mAl);
        obj.AddMember("byteOffset", static_cast<uint64_t>(bv.byteOffset), w.mAl);
        obj.AddMember("byteLength", static_cast<uint64_t>(bv.byteLength), w.mAl);
        if (bv.target != BufferViewTarget_NONE) {
            obj.AddMember("target", int(bv.target), w.mAl);
        }
    }

    inline void Write(Value& /*obj*/, Camera& /*c*/, AssetWriter& /*w*/)
    {

    }

    inline void Write(Value& obj, Image& img, AssetWriter& w)
    {
        std::string uri;
        if (w.mAsset.extensionsUsed.KHR_binary_glTF && img.bufferView) {
            Value exts, ext;
            exts.SetObject();
            ext.SetObject();

            ext.AddMember("bufferView", StringRef(img.bufferView->id), w.mAl);

            if (!img.mimeType.empty())
                ext.AddMember("mimeType", StringRef(img.mimeType), w.mAl);

            exts.AddMember("KHR_binary_glTF", ext, w.mAl);
            obj.AddMember("extensions", exts, w.mAl);
            return;
        }
        else if (img.HasData()) {
            uri = "data:" + (img.mimeType.empty() ? "application/octet-stream" : img.mimeType);
            uri += ";base64,";
            Base64::Encode(img.GetData(), img.GetDataLength(), uri);
        }
        else {
            uri = img.uri;
        }

        obj.AddMember("uri", Value(uri, w.mAl).Move(), w.mAl);
    }

    namespace {
        inline void WriteColorOrTex(Value& obj, TexProperty& prop, const char* propName, MemoryPoolAllocator<>& al)
        {
            if (prop.texture)
                obj.AddMember(StringRef(propName), Value(prop.texture->id, al).Move(), al);
            else {
                Value col;
                obj.AddMember(StringRef(propName), MakeValue(col, prop.color, al), al);
            }
        }
    }

    inline void Write(Value& obj, Material& m, AssetWriter& w)
    {
        Value v;
        v.SetObject();
        {
            WriteColorOrTex(v, m.ambient, "ambient", w.mAl);
            WriteColorOrTex(v, m.diffuse, "diffuse", w.mAl);
            WriteColorOrTex(v, m.specular, "specular", w.mAl);
            WriteColorOrTex(v, m.emission, "emission", w.mAl);

            if (m.transparent)
                v.AddMember("transparency", m.transparency, w.mAl);

            v.AddMember("shininess", m.shininess, w.mAl);
        }
        obj.AddMember("values", v, w.mAl);
    }

    namespace {
        inline void WriteAttrs(AssetWriter& w, Value& attrs, Mesh::AccessorList& lst,
            const char* semantic, bool forceNumber = false)
        {
            if (lst.empty()) return;
            if (lst.size() == 1 && !forceNumber) {
                attrs.AddMember(StringRef(semantic), Value(lst[0]->id, w.mAl).Move(), w.mAl);
            }
            else {
                for (size_t i = 0; i < lst.size(); ++i) {
                    char buffer[32];
                    ai_snprintf(buffer, 32, "%s_%d", semantic, int(i));
                    attrs.AddMember(Value(buffer, w.mAl).Move(), Value(lst[i]->id, w.mAl).Move(), w.mAl);
                }
            }
        }
    }

    inline void Write(Value& obj, Mesh& m, AssetWriter& w)
    {
		/********************* Name **********************/
		obj.AddMember("name", m.name, w.mAl);

		/****************** Primitives *******************/
        Value primitives;
        primitives.SetArray();
        primitives.Reserve(unsigned(m.primitives.size()), w.mAl);

        for (size_t i = 0; i < m.primitives.size(); ++i) {
            Mesh::Primitive& p = m.primitives[i];
            Value prim;
            prim.SetObject();
            {
                prim.AddMember("mode", Value(int(p.mode)).Move(), w.mAl);

                if (p.material)
                    prim.AddMember("material", p.material->id, w.mAl);

                if (p.indices)
                    prim.AddMember("indices", Value(p.indices->id, w.mAl).Move(), w.mAl);

                Value attrs;
                attrs.SetObject();
                {
                    WriteAttrs(w, attrs, p.attributes.position, "POSITION");
                    WriteAttrs(w, attrs, p.attributes.normal, "NORMAL");
                    WriteAttrs(w, attrs, p.attributes.texcoord, "TEXCOORD", true);
                    WriteAttrs(w, attrs, p.attributes.color, "COLOR");
                    WriteAttrs(w, attrs, p.attributes.joint, "JOINT");
                    WriteAttrs(w, attrs, p.attributes.jointmatrix, "JOINTMATRIX");
                    WriteAttrs(w, attrs, p.attributes.weight, "WEIGHT");
                }
                prim.AddMember("attributes", attrs, w.mAl);
            }
            primitives.PushBack(prim, w.mAl);
        }

        obj.AddMember("primitives", primitives, w.mAl);
    }

    inline void Write(Value& obj, Node& n, AssetWriter& w)
    {

        if (n.matrix.isPresent) {
            Value val;
            obj.AddMember("matrix", MakeValue(val, n.matrix.value, w.mAl).Move(), w.mAl);
        }

        if (n.translation.isPresent) {
            Value val;
            obj.AddMember("translation", MakeValue(val, n.translation.value, w.mAl).Move(), w.mAl);
        }

        if (n.scale.isPresent) {
            Value val;
            obj.AddMember("scale", MakeValue(val, n.scale.value, w.mAl).Move(), w.mAl);
        }
        if (n.rotation.isPresent) {
            Value val;
            obj.AddMember("rotation", MakeValue(val, n.rotation.value, w.mAl).Move(), w.mAl);
        }

        AddRefsVector(obj, "children", n.children, w.mAl);

        AddRefsVector(obj, "meshes", n.meshes, w.mAl);

        AddRefsVector(obj, "skeletons", n.skeletons, w.mAl);

        if (n.skin) {
            obj.AddMember("skin", Value(n.skin->id, w.mAl).Move(), w.mAl);
        }

        if (!n.jointName.empty()) {
          obj.AddMember("jointName", n.jointName, w.mAl);
        }
    }

    inline void Write(Value& /*obj*/, Program& /*b*/, AssetWriter& /*w*/)
    {

    }

    inline void Write(Value& obj, Sampler& b, AssetWriter& w)
    {
        if (b.wrapS) {
            obj.AddMember("wrapS", b.wrapS, w.mAl);
        }
        if (b.wrapT) {
            obj.AddMember("wrapT", b.wrapT, w.mAl);
        }
        if (b.magFilter) {
            obj.AddMember("magFilter", b.magFilter, w.mAl);
        }
        if (b.minFilter) {
            obj.AddMember("minFilter", b.minFilter, w.mAl);
        }
    }

    inline void Write(Value& scene, Scene& s, AssetWriter& w)
    {
        AddRefsVector(scene, "nodes", s.nodes, w.mAl);
    }

    inline void Write(Value& /*obj*/, Shader& /*b*/, AssetWriter& /*w*/)
    {

    }

    inline void Write(Value& obj, Skin& b, AssetWriter& w)
    {
        /****************** jointNames *******************/
        Value vJointNames;
        vJointNames.SetArray();
        vJointNames.Reserve(unsigned(b.jointNames.size()), w.mAl);

        for (size_t i = 0; i < unsigned(b.jointNames.size()); ++i) {
            vJointNames.PushBack(StringRef(b.jointNames[i]->jointName), w.mAl);
        }
        obj.AddMember("jointNames", vJointNames, w.mAl);

        if (b.bindShapeMatrix.isPresent) {
            Value val;
            obj.AddMember("bindShapeMatrix", MakeValue(val, b.bindShapeMatrix.value, w.mAl).Move(), w.mAl);
        }

        if (b.inverseBindMatrices) {
            obj.AddMember("inverseBindMatrices", Value(b.inverseBindMatrices->id, w.mAl).Move(), w.mAl);
        }

    }

    inline void Write(Value& /*obj*/, Technique& /*b*/, AssetWriter& /*w*/)
    {

    }

    inline void Write(Value& obj, Texture& tex, AssetWriter& w)
    {
        if (tex.source) {
            obj.AddMember("source", Value(tex.source->id, w.mAl).Move(), w.mAl);
        }
        if (tex.sampler) {
            obj.AddMember("sampler", Value(tex.sampler->id, w.mAl).Move(), w.mAl);
        }
    }

    inline void Write(Value& /*obj*/, Light& /*b*/, AssetWriter& /*w*/)
    {

    }


    inline AssetWriter::AssetWriter(Asset& a)
        : mDoc()
        , mAsset(a)
        , mAl(mDoc.GetAllocator())
    {
        mDoc.SetObject();

        // Dump the contents of the dictionaries
        for (size_t i = 0; i < a.mDicts.size(); ++i) {
            a.mDicts[i]->WriteObjects(*this);
        }

        // Add the target scene field
        if (mAsset.scene) {
            mDoc.AddMember("scene", StringRef(mAsset.scene->id), mAl);
        }
    }

    inline void AssetWriter::WriteFile(const char* path)
    {
        std::unique_ptr<IOStream> jsonOutFile(mAsset.OpenFile(path, "wt", true));

        if (jsonOutFile == 0) {
            throw DeadlyExportError("Could not open output file: " + std::string(path));
        }

        StringBuffer docBuffer;

        PrettyWriter<StringBuffer> writer(docBuffer);
        if (!mDoc.Accept(writer)) {
            throw DeadlyExportError("Failed to write scene data!");
        }

        if (jsonOutFile->Write(docBuffer.GetString(), docBuffer.GetSize(), 1) != 1) {
            throw DeadlyExportError("Failed to write scene data!");
        }

        // Write buffer data to separate .bin files
        for (unsigned int i = 0; i < mAsset.buffers.Size(); ++i) {
            Ref<Buffer> b = mAsset.buffers.Get(i);

            std::string binPath = b->GetURI();

            std::unique_ptr<IOStream> binOutFile(mAsset.OpenFile(binPath, "wb", true));

            if (binOutFile == 0) {
                throw DeadlyExportError("Could not open output file: " + binPath);
            }

            if (b->byteLength > 0) {
                if (binOutFile->Write(b->GetPointer(), b->byteLength, 1) != 1) {
                    throw DeadlyExportError("Failed to write binary file: " + binPath);
                }
            }
        }
    }

#if _MSC_VER
#    pragma warning(pop)
#endif // _WIN32

}
