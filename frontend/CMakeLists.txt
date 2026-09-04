include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQmlModule)

find_package(Qt6 6.6 REQUIRED COMPONENTS Qml Quick)
find_package(KF6 6.0 REQUIRED COMPONENTS I18n)
find_package(Plasma 6.0 REQUIRED)

add_subdirectory(qmlmodule)
add_subdirectory(plasmoid)
