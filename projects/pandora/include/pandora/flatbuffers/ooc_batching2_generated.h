// automatically generated by the FlatBuffers compiler, do not modify


#ifndef FLATBUFFERS_GENERATED_OOCBATCHING2_PANDORA_SERIALIZATION_H_
#define FLATBUFFERS_GENERATED_OOCBATCHING2_PANDORA_SERIALIZATION_H_

#include "flatbuffers/flatbuffers.h"

#include "contiguous_allocator_generated.h"
#include "data_types_generated.h"
#include "scene_generated.h"
#include "triangle_mesh_generated.h"
#include "wive_bvh8_generated.h"

namespace pandora {
namespace serialization {

struct OOCBatchingBaseSceneObject;

struct OOCBatchingBaseSceneObject FLATBUFFERS_FINAL_CLASS : private flatbuffers::Table {
  enum {
    VT_BASE_GEOMETRY = 4,
    VT_BVH = 6
  };
  const GeometricSceneObjectGeometry *base_geometry() const {
    return GetPointer<const GeometricSceneObjectGeometry *>(VT_BASE_GEOMETRY);
  }
  GeometricSceneObjectGeometry *mutable_base_geometry() {
    return GetPointer<GeometricSceneObjectGeometry *>(VT_BASE_GEOMETRY);
  }
  const WiVeBVH8 *bvh() const {
    return GetPointer<const WiVeBVH8 *>(VT_BVH);
  }
  WiVeBVH8 *mutable_bvh() {
    return GetPointer<WiVeBVH8 *>(VT_BVH);
  }
  bool Verify(flatbuffers::Verifier &verifier) const {
    return VerifyTableStart(verifier) &&
           VerifyOffset(verifier, VT_BASE_GEOMETRY) &&
           verifier.VerifyTable(base_geometry()) &&
           VerifyOffset(verifier, VT_BVH) &&
           verifier.VerifyTable(bvh()) &&
           verifier.EndTable();
  }
};

struct OOCBatchingBaseSceneObjectBuilder {
  flatbuffers::FlatBufferBuilder &fbb_;
  flatbuffers::uoffset_t start_;
  void add_base_geometry(flatbuffers::Offset<GeometricSceneObjectGeometry> base_geometry) {
    fbb_.AddOffset(OOCBatchingBaseSceneObject::VT_BASE_GEOMETRY, base_geometry);
  }
  void add_bvh(flatbuffers::Offset<WiVeBVH8> bvh) {
    fbb_.AddOffset(OOCBatchingBaseSceneObject::VT_BVH, bvh);
  }
  explicit OOCBatchingBaseSceneObjectBuilder(flatbuffers::FlatBufferBuilder &_fbb)
        : fbb_(_fbb) {
    start_ = fbb_.StartTable();
  }
  OOCBatchingBaseSceneObjectBuilder &operator=(const OOCBatchingBaseSceneObjectBuilder &);
  flatbuffers::Offset<OOCBatchingBaseSceneObject> Finish() {
    const auto end = fbb_.EndTable(start_);
    auto o = flatbuffers::Offset<OOCBatchingBaseSceneObject>(end);
    return o;
  }
};

inline flatbuffers::Offset<OOCBatchingBaseSceneObject> CreateOOCBatchingBaseSceneObject(
    flatbuffers::FlatBufferBuilder &_fbb,
    flatbuffers::Offset<GeometricSceneObjectGeometry> base_geometry = 0,
    flatbuffers::Offset<WiVeBVH8> bvh = 0) {
  OOCBatchingBaseSceneObjectBuilder builder_(_fbb);
  builder_.add_bvh(bvh);
  builder_.add_base_geometry(base_geometry);
  return builder_.Finish();
}

inline const pandora::serialization::OOCBatchingBaseSceneObject *GetOOCBatchingBaseSceneObject(const void *buf) {
  return flatbuffers::GetRoot<pandora::serialization::OOCBatchingBaseSceneObject>(buf);
}

inline const pandora::serialization::OOCBatchingBaseSceneObject *GetSizePrefixedOOCBatchingBaseSceneObject(const void *buf) {
  return flatbuffers::GetSizePrefixedRoot<pandora::serialization::OOCBatchingBaseSceneObject>(buf);
}

inline OOCBatchingBaseSceneObject *GetMutableOOCBatchingBaseSceneObject(void *buf) {
  return flatbuffers::GetMutableRoot<OOCBatchingBaseSceneObject>(buf);
}

inline bool VerifyOOCBatchingBaseSceneObjectBuffer(
    flatbuffers::Verifier &verifier) {
  return verifier.VerifyBuffer<pandora::serialization::OOCBatchingBaseSceneObject>(nullptr);
}

inline bool VerifySizePrefixedOOCBatchingBaseSceneObjectBuffer(
    flatbuffers::Verifier &verifier) {
  return verifier.VerifySizePrefixedBuffer<pandora::serialization::OOCBatchingBaseSceneObject>(nullptr);
}

inline void FinishOOCBatchingBaseSceneObjectBuffer(
    flatbuffers::FlatBufferBuilder &fbb,
    flatbuffers::Offset<pandora::serialization::OOCBatchingBaseSceneObject> root) {
  fbb.Finish(root);
}

inline void FinishSizePrefixedOOCBatchingBaseSceneObjectBuffer(
    flatbuffers::FlatBufferBuilder &fbb,
    flatbuffers::Offset<pandora::serialization::OOCBatchingBaseSceneObject> root) {
  fbb.FinishSizePrefixed(root);
}

}  // namespace serialization
}  // namespace pandora

#endif  // FLATBUFFERS_GENERATED_OOCBATCHING2_PANDORA_SERIALIZATION_H_
