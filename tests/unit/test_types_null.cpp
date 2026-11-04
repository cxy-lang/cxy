/**
 * Unit Tests: the type predicates and the strip functions accept NULL, so
 * callers do not need a `type && isFoo(type)` guard.
 */

#include "doctest.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"

TEST_CASE("Type predicates answer false for NULL")
{
    const Type *none = nullptr;
    CHECK_FALSE(isPrimitiveType(none));
    CHECK_FALSE(isIntegerType(none));
    CHECK_FALSE(isIntegralType(none));
    CHECK_FALSE(isSignedType(none));
    CHECK_FALSE(isUnsignedType(none));
    CHECK_FALSE(isFloatType(none));
    CHECK_FALSE(isNumericType(none));
    CHECK_FALSE(isBooleanType(none));
    CHECK_FALSE(isCharacterType(none));
    CHECK_FALSE(isArrayType(none));
    CHECK_FALSE(isSliceType(none));
    CHECK_FALSE(isPointerType(none));
    CHECK_FALSE(isNullableType(none));
    CHECK_FALSE(isReferenceType(none));
    CHECK_FALSE(isPointerOrReferenceType(none));
    CHECK_FALSE(isVoidType(none));
    CHECK_FALSE(isClosureType(none));
    CHECK_FALSE(isClassType(none));
    CHECK_FALSE(isOptionalType(none));
    CHECK_FALSE(isComplexType(none));
    CHECK_FALSE(isStructType(none));
    CHECK_FALSE(isUnionType(none));
    CHECK_FALSE(isTupleType(none));
    CHECK_FALSE(isConstType(none));
    CHECK_FALSE(isResultType(none));
    CHECK_FALSE(isVoidResultType(none));
    CHECK_FALSE(isExceptionType(none));
    CHECK_FALSE(isBaseExceptionType(none));
    CHECK_FALSE(isDestructible(none));
    CHECK_FALSE(typeIs(none, Func));
}

TEST_CASE("Strip and resolve functions give NULL back for NULL")
{
    const Type *none = nullptr;
    u64 flags = flgNone;
    CHECK(resolveType(none) == nullptr);
    CHECK(resolveAndUnThisType(none) == nullptr);
    CHECK(resolveUnThisUnwrapType(none) == nullptr);
    CHECK(stripPointer(none) == nullptr);
    CHECK(stripReference(none) == nullptr);
    CHECK(stripPointerOrReference(none) == nullptr);
    CHECK(stripAll(none) == nullptr);
    CHECK(stripPointerOnce(none, &flags) == nullptr);
    CHECK(stripPointerOrReferenceOnce(none, &flags) == nullptr);
    CHECK(unwrapType(none, &flags) == nullptr);
    CHECK(flags == flgNone);
}
