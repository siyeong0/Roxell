set(GOOGLETEST_VERSION 1.18.0)
set(gtest_force_shared_crt ON)
set(INSTALL_GTEST OFF)
add_subdirectory("${PROJECT_SOURCE_DIR}/ThirdParty/googletest/googletest" "${CMAKE_CURRENT_BINARY_DIR}/ThirdParty/googletest")
include(GoogleTest)

function(roxell_add_core_tests component)
    set(target "Test-Core${component}")
    set(sources ${ARGN})
    list(TRANSFORM sources PREPEND "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/${component}/Tests/")
    add_executable(${target} ${sources})
    target_link_libraries(${target} PRIVATE Runtime-Core GTest::gtest_main)
    set_property(TARGET ${target} PROPERTY FOLDER "Tests/Core")
    gtest_discover_tests(${target}
        TEST_PREFIX "Roxell.Core.${component}."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES TIMEOUT 120
    )
endfunction()

roxell_add_core_tests(Algorithm
    AlgorithmsTests.cpp
)

roxell_add_core_tests(Async
    CancellationTests.cpp
    JobContextTests.cpp
    JobSchedulerTests.cpp
    MpscQueueTests.cpp
    SpscQueueTests.cpp
    TaskCombinatorsTests.cpp
    TaskHandleTests.cpp
    TaskSourceTests.cpp
    TaskTests.cpp
    ThreadTests.cpp
)

roxell_add_core_tests(Base
    ByteOperationsTests.cpp
    ByteOrderTests.cpp
    ByteReaderTests.cpp
    ByteWriterTests.cpp
    CharacterTraitsTests.cpp
    ClockPrimitivesTests.cpp
    ComparisonTests.cpp
    ComparisonTypesTests.cpp
    CoreAbiTests.cpp
    DiagnosticTests.cpp
    DurationTests.cpp
    HashFunctionTests.cpp
    HashTests.cpp
    IteratorTypesTests.cpp
    NamePoolTests.cpp
    NameTests.cpp
    NativeRuntimeTests.cpp
    NumericLimitsTests.cpp
    ResultTests.cpp
    RevisionTests.cpp
    ScopeExitTests.cpp
    Sha256Tests.cpp
    SourceLocationTests.cpp
    StrongIdTests.cpp
    SystemErrorTests.cpp
    TextPrimitivesTests.cpp
    ThreadPrimitivesTests.cpp
    TraceCapture_codecTests.cpp
    TraceCapture_generationTests.cpp
    TypeTraitsTests.cpp
    TypeVocabularyTests.cpp
    TypesTests.cpp
    UtilityTests.cpp
    VocabularyTests.cpp
)

roxell_add_core_tests(Containers
    ArrayTests.cpp
    BinaryHeapTests.cpp
    BitArrayTests.cpp
    CoreStringTests.cpp
    FixedStringTests.cpp
    FlatMapTests.cpp
    FlatSetTests.cpp
    FunctionRefTests.cpp
    FunctionTargetTests.cpp
    HashMapTests.cpp
    HashSetTests.cpp
    HashingTests.cpp
    InplaceVectorTests.cpp
    IntrusiveListTests.cpp
    OrderingTests.cpp
    PagedArrayTests.cpp
    PriorityQueueTests.cpp
    RingBufferTests.cpp
    SlotMapTests.cpp
    SmallVectorTests.cpp
    SpanTests.cpp
    StringBuilderTests.cpp
    StringViewTests.cpp
    TextConversionTests.cpp
    UniqueFunctionTests.cpp
    Utf8Tests.cpp
)

roxell_add_core_tests(Diagnostics
    ConsoleIoTests.cpp
    ITraceSinkTests.cpp
    LogTests.cpp
    TraceScopeTests.cpp
    TraceTests.cpp
    TraceTypesTests.cpp
)

add_executable(Test-CoreAssertionProcess "${CMAKE_CURRENT_LIST_DIR}/Diagnostics/Tests/AssertionProcessTests.cpp")
target_link_libraries(Test-CoreAssertionProcess PRIVATE Runtime-Core GTest::gtest)
set_property(TARGET Test-CoreAssertionProcess PROPERTY FOLDER "Tests/Core")
gtest_discover_tests(Test-CoreAssertionProcess
    TEST_PREFIX "Roxell.Core.Diagnostics."
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 30
)

roxell_add_core_tests(Math
    AffineTransform2DTests.cpp
    AffineTransformTests.cpp
    BatchMathTests.cpp
    BoxTests.cpp
    CapsuleTests.cpp
    CircleTests.cpp
    ColorTests.cpp
    CurveMathTests.cpp
    CurveMath_vectorTests.cpp
    DampingTests.cpp
    EasingTests.cpp
    FloatEnvironmentTests.cpp
    FrustumTests.cpp
    GeometryQueriesTests.cpp
    GeometryVolumeQueriesTests.cpp
    GridMathTests.cpp
    Int2Tests.cpp
    Int3Tests.cpp
    Int4Tests.cpp
    IntegerAlgorithmsTests.cpp
    LineSegmentTests.cpp
    MathConstantsTests.cpp
    MathScalarTests.cpp
    Matrix3x3Tests.cpp
    Matrix4x4Tests.cpp
    NumericTests.cpp
    OrientedBoxTests.cpp
    PlaneTests.cpp
    ProjectionMathTests.cpp
    QuaternionTests.cpp
    RandomAlgorithmsTests.cpp
    RandomNoiseTests.cpp
    RandomSamplingTests.cpp
    RandomStreamTests.cpp
    RayTests.cpp
    RectIntTests.cpp
    RectTests.cpp
    ScalarMathTests.cpp
    SphereTests.cpp
    SpringTests.cpp
    TransformTests.cpp
    TriangleTests.cpp
    Vector2Tests.cpp
    Vector3Tests.cpp
    Vector4Tests.cpp
)

roxell_add_core_tests(Memory
    BudgetMemoryResourceTests.cpp
    FailingMemoryResourceTests.cpp
    InterfaceOwnerTests.cpp
    LinearMemoryResourceTests.cpp
    LinearMemoryScopeTests.cpp
    MemoryBlockTests.cpp
    MemoryOperationsTests.cpp
    MemoryResourceTests.cpp
    ObjectPoolTests.cpp
    PoolMemoryResourceTests.cpp
    ScratchMemoryTests.cpp
    SharedPtrTests.cpp
    SystemMemoryResourceTests.cpp
    TrackingMemoryResourceTests.cpp
    UniquePtrTests.cpp
)

roxell_add_core_tests(Schema
    CanonicalJsonCodecTests.cpp
    SchemaCatalogJsonTests.cpp
    SchemaCatalogTests.cpp
    SchemaRegistryTests.cpp
    SchemaRegistry_boundaryTests.cpp
    ValueTests.cpp
    ValueTraversalTests.cpp
)
