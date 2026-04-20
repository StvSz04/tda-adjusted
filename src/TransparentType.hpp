#pragma once

#include "Utils/PrintUtils.hpp"

#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>

#include <list>
#include <memory>
#include <ranges>
#include <set>

namespace tda {

class TransparentType;

class TransparentTypeFactory {
public:
  static std::unique_ptr<TransparentType> createFromValue(const llvm::Value* value);
  static std::unique_ptr<TransparentType> createFromType(llvm::Type* unwrappedType, unsigned indirections = 0);
  static std::unique_ptr<TransparentType> createFromExisting(const TransparentType* unwrappedType,
                                                             unsigned indirections = 0);
  static std::unique_ptr<TransparentType>
  createFromFields(llvm::SmallVector<std::unique_ptr<TransparentType>>& fieldTypes,
                   const llvm::SmallVector<unsigned>& fieldOffsets,
                   const llvm::SmallVector<unsigned>& fieldSizes,
                   unsigned indirections = 0);
};

class TransparentType : public Printable {
  friend TransparentTypeFactory;

public:
  enum TransparentTypeKind {
    K_Primitive,
    K_Pointer,
    K_Array,
    K_Struct
  };

  static bool classof(const TransparentType* type) { return type->getKind() == K_Primitive; }

  virtual TransparentTypeKind getKind() const { return K_Primitive; }
  virtual bool isCompatibleLLVMType(llvm::Type* type) const { return type->getNumContainedTypes() == 0; }

  TransparentType() = default;

  llvm::Type* getLLVMType() const { return llvmType; }
  void setLLVMType(llvm::Type* llvmType) {
    if (llvmType)
      assert(isCompatibleLLVMType(llvmType));
    this->llvmType = llvmType;
  }

  virtual const TransparentType* getFullyUnwrappedType() const { return this; }
  virtual TransparentType* getFullyUnwrappedType() { return this; }
  virtual TransparentType* getPointedType() const;
  std::unique_ptr<TransparentType> getPointerToType() const;

  virtual bool isOpaquePtr() const { return false; }
  virtual bool containsOpaquePtr() const { return false; }
  bool isPlaceholder() const { return isPrimitiveTT() && !llvmType; }
  bool isUnion() const { return isPrimitiveTT() && isAUnion; }

  bool isPrimitiveTT() const { return getKind() == K_Primitive; }
  bool isPointerTT() const { return getKind() == K_Pointer; }
  bool isArrayTT() const { return getKind() == K_Array; }
  bool isStructTT() const { return getKind() == K_Struct; }

  virtual bool isPrimitiveTTOrPtrTo() const { return isPrimitiveTT(); }
  virtual bool isArrayTTOrPtrTo() const { return isArrayTT(); }
  virtual bool isStructTTOrPtrTo() const { return isStructTT(); }

  virtual const TransparentType* getFirstNonPtr() const { return this; }
  virtual TransparentType* getFirstNonPtr() { return this; }

  bool isVoidTy() const { return llvmType && llvmType->isVoidTy(); }
  virtual bool isByteTyOrPtrTo() const { return llvmType && llvmType == llvm::Type::getInt8Ty(llvmType->getContext()); }
  virtual bool isIntegerTyOrPtrTo() const { return llvmType && llvmType->isIntegerTy(); }
  virtual bool isFloatingPointTyOrPtrTo() const { return llvmType && llvmType->isFloatingPointTy(); }

  virtual llvm::SmallPtrSet<llvm::Type*, 4> getContainedLLVMTypes() const;
  virtual bool containsFloatingPointType() const { return llvmType->isFloatingPointTy(); }
  llvm::Type* toLLVMType() const { return llvmType; }

  std::unique_ptr<TransparentType>
  getIndexedType(const TransparentType* gepSrcElType,
                 std::optional<llvm::iterator_range<llvm::Use*>> gepIndices = std::nullopt) const;

  std::unique_ptr<TransparentType>
  cloneAndSetIndexedType(const TransparentType* setType,
                         const TransparentType* gepSrcElType,
                         std::optional<llvm::iterator_range<llvm::Use*>> gepIndices = std::nullopt) const;

  bool isStructurallyEquivalent(const TransparentType* other) const;

  virtual bool operator==(const TransparentType& other) const;
  bool operator!=(const TransparentType& other) const { return !(*this == other); }

  virtual bool isCompatibleWith(const TransparentType* other) const;
  virtual std::unique_ptr<TransparentType> mergeWith(const TransparentType* other) const;

  virtual std::unique_ptr<TransparentType> clone() const;
  std::string toString() const override;

protected:
  llvm::Type* llvmType = nullptr;
  bool isAUnion = false;

  TransparentType(const TransparentType& other) = default;

  TransparentType(llvm::Type* unwrappedType, bool isUnion = false)
  : llvmType(unwrappedType), isAUnion(isUnion) {}

  const TransparentType* findGepSrcElementType(const TransparentType* type) const;

  std::unique_ptr<TransparentType> getOrSetIndexedType(const TransparentType* gepSrcElType,
                                                       std::optional<llvm::iterator_range<llvm::Use*>> gepIndices,
                                                       const TransparentType* setType = nullptr,
                                                       bool set = false) const;

  TransparentType* getOrSetIndexedType(TransparentType* ptrOperandType,
                                       const TransparentType* gepSrcElType,
                                       std::list<const llvm::Value*>& gepIndices,
                                       const TransparentType* mergeType = nullptr) const;
};

class TransparentPointerType : public TransparentType {
  friend TransparentTypeFactory;

public:
  static bool classof(const TransparentType* type) { return type->getKind() == K_Pointer; }

  TransparentTypeKind getKind() const override { return K_Pointer; }
  bool isCompatibleLLVMType(llvm::Type* type) const override { return type->isPointerTy(); }

  TransparentPointerType() = default;

  TransparentPointerType(const TransparentPointerType& other)
  : TransparentType(other), pointedType(other.pointedType ? other.pointedType->clone() : nullptr) {}

  TransparentType* getPointedType() const override { return pointedType ? pointedType.get() : nullptr; }
  void setPointedType(std::unique_ptr<TransparentType> pointedType) { this->pointedType = std::move(pointedType); }

  const TransparentType* getFullyUnwrappedType() const override {
    return pointedType ? pointedType->getFullyUnwrappedType() : this;
  }
  TransparentType* getFullyUnwrappedType() override {
    return pointedType ? pointedType->getFullyUnwrappedType() : this;
  }

  bool isOpaquePtr() const override { return !pointedType; }
  bool containsOpaquePtr() const override { return !pointedType || pointedType->containsOpaquePtr(); }

  bool isPrimitiveTTOrPtrTo() const override { return pointedType && pointedType->isPrimitiveTT(); }
  bool isArrayTTOrPtrTo() const override { return pointedType && pointedType->isArrayTT(); }
  bool isStructTTOrPtrTo() const override { return pointedType && pointedType->isStructTT(); }

  const TransparentType* getFirstNonPtr() const override { return pointedType ? pointedType.get() : nullptr; }
  TransparentType* getFirstNonPtr() override { return pointedType ? pointedType.get() : nullptr; }

  bool isByteTyOrPtrTo() const override { return pointedType && pointedType->isByteTyOrPtrTo(); }
  bool isIntegerTyOrPtrTo() const override { return pointedType && pointedType->isIntegerTyOrPtrTo(); }
  bool isFloatingPointTyOrPtrTo() const override { return pointedType && pointedType->isFloatingPointTyOrPtrTo(); }

  llvm::SmallPtrSet<llvm::Type*, 4> getContainedLLVMTypes() const override;
  bool containsFloatingPointType() const override;

  bool operator==(const TransparentType& other) const override;

  bool isCompatibleWith(const TransparentType* other) const override;
  std::unique_ptr<TransparentType> mergeWith(const TransparentType* other) const override;

  std::unique_ptr<TransparentType> clone() const override;
  std::string toString() const override;

protected:
  std::unique_ptr<TransparentType> pointedType;

  TransparentPointerType(llvm::PointerType* llvmType, std::unique_ptr<TransparentType> pointedType = nullptr)
  : TransparentType(llvmType), pointedType(std::move(pointedType)) {}
};

class TransparentArrayType : public TransparentType {
  friend TransparentTypeFactory;

public:
  static bool classof(const TransparentType* type) { return type->getKind() == K_Array; }

  TransparentTypeKind getKind() const override { return K_Array; }
  bool isCompatibleLLVMType(llvm::Type* type) const override { return type->isArrayTy() || type->isVectorTy(); }

  TransparentArrayType() = default;

  const TransparentType* getFullyUnwrappedType() const override { return getElementType()->getFullyUnwrappedType(); }
  TransparentType* getFullyUnwrappedType() override { return getElementType()->getFullyUnwrappedType(); }
  bool containsOpaquePtr() const override;

  llvm::SmallPtrSet<llvm::Type*, 4> getContainedLLVMTypes() const override;
  bool containsFloatingPointType() const override { return getElementType()->containsFloatingPointType(); }

  TransparentType* getElementType() const { return elementType.get(); }
  void setElementType(std::unique_ptr<TransparentType> elementType) { this->elementType = std::move(elementType); }
  unsigned getNumElements() const;

  bool operator==(const TransparentType& other) const override;

  bool isCompatibleWith(const TransparentType* other) const override;
  std::unique_ptr<TransparentType> mergeWith(const TransparentType* other) const override;

  std::unique_ptr<TransparentType> clone() const override;
  std::string toString() const override;

protected:
  std::unique_ptr<TransparentType> elementType;

  TransparentArrayType(const TransparentArrayType& other)
  : TransparentType(other), elementType(other.elementType->clone()) {}

  TransparentArrayType(llvm::ArrayType* arrayType)
  : TransparentType(arrayType) {
    elementType = TransparentTypeFactory::createFromType(arrayType->getElementType(), 0);
  }

  TransparentArrayType(llvm::VectorType* vecType)
  : TransparentType(vecType) {
    elementType = TransparentTypeFactory::createFromType(vecType->getElementType(), 0);
  }
};

class TransparentStructType : public TransparentType {
  friend TransparentTypeFactory;

public:
  static bool classof(const TransparentType* type) { return type->getKind() == K_Struct; }

  TransparentTypeKind getKind() const override { return K_Struct; }
  bool isCompatibleLLVMType(llvm::Type* type) const override { return type->isStructTy(); }

  TransparentStructType() = default;

  bool containsOpaquePtr() const override;

  llvm::SmallPtrSet<llvm::Type*, 4> getContainedLLVMTypes() const override;
  bool containsFloatingPointType() const override;

  TransparentType* getFieldType(unsigned i) const { return fieldTypes[i].get(); }
  void setFieldType(unsigned i, std::unique_ptr<TransparentType> fieldType) { fieldTypes[i] = std::move(fieldType); }
  void addFieldType(std::unique_ptr<TransparentType> fieldType) { fieldTypes.push_back(std::move(fieldType)); }
  unsigned getNumFieldTypes() const { return fieldTypes.size(); }

  auto getFieldTypes() const {
    return fieldTypes | std::views::transform([](auto& smart_ptr) { return smart_ptr.get(); });
  }

  unsigned getFieldOffset(unsigned i) const { return fieldOffsets[i]; }

  bool isFieldPadding(unsigned i) const { return llvm::is_contained(paddingFields, i); }
  void addFieldPadding(unsigned i) { paddingFields.insert(i); }
  unsigned getNumPaddingFields() const { return paddingFields.size(); }

  std::set<unsigned> getPaddingFields() const { return paddingFields; }

  bool operator==(const TransparentType& other) const override;

  bool isCompatibleWith(const TransparentType* other) const override;
  std::unique_ptr<TransparentType> mergeWith(const TransparentType* other) const override;

  std::unique_ptr<TransparentType> clone() const override;
  std::string toString() const override;

protected:
  llvm::SmallVector<std::unique_ptr<TransparentType>, 8> fieldTypes;
  llvm::SmallVector<unsigned> fieldOffsets;
  std::set<unsigned> paddingFields; // TODO remove

  TransparentStructType(const TransparentStructType& other)
  : TransparentType(other) {
    for (const auto& field : other.fieldTypes)
      fieldTypes.push_back(field->clone());
    for (const auto& fieldOffset : other.fieldOffsets)
      fieldOffsets.push_back(fieldOffset);
    for (const auto& paddingFieldIdx : other.paddingFields)
      paddingFields.insert(paddingFieldIdx);
  }

  TransparentStructType(llvm::StructType* unwrappedType);

  TransparentStructType(llvm::SmallVector<std::unique_ptr<TransparentType>>& fieldTypes,
                        const llvm::SmallVector<unsigned>& fieldOffsets,
                        const llvm::SmallVector<unsigned>& fieldSizes);
};

} // namespace tda
