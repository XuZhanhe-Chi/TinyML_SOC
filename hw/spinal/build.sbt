ThisBuild / version := "0.1.0"
ThisBuild / scalaVersion := "2.13.14"
ThisBuild / organization := "TinyML_SOC"

val spinalVersion = "1.12.3"
val spinalCore = "com.github.spinalhdl" %% "spinalhdl-core" % spinalVersion
val spinalLib = "com.github.spinalhdl" %% "spinalhdl-lib" % spinalVersion
val spinalIdslPlugin = compilerPlugin("com.github.spinalhdl" %% "spinalhdl-idsl-plugin" % spinalVersion)
val snakeYaml = "org.yaml" % "snakeyaml" % "2.2"

lazy val projectname = (project in file("."))
  .settings(
    name := "tinyml-soc-hw",
    Compile / scalaSource := baseDirectory.value / "src" / "main" / "scala",
    Compile / unmanagedSourceDirectories ++= Seq(
      (baseDirectory.value / "../../third_party/TinyML_NPU/hw/spinal/src/main/scala").getCanonicalFile,
      (baseDirectory.value / "../../third_party/VexRiscv/src/main/scala").getCanonicalFile
    ),
    libraryDependencies ++= Seq(
      spinalCore,
      spinalLib,
      spinalIdslPlugin,
      snakeYaml
    )
  )

scalacOptions ++= Seq(
  "-deprecation",
  "-feature",
  "-language:postfixOps"
)

fork := true
