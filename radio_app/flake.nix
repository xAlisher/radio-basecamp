{
  description = "radio — decentralized audio broadcast (ui_qml module with C++ backend; consumes delivery_module)";

  # ui_qml-with-C++-backend is the SUPPORTED path for consuming delivery_module — a type:core
  # module crashes in new LogosModules()/getClient (upstream delivery #31). Mirrors logos-delivery-demo.
  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    # Pin nixpkgs/bundler to the builder's so the Rust/zerokit toolchain delivery pulls resolves
    # from the shared binary cache instead of building from source (v0.1.2's zerokit-2.0.2 fails).
    nixpkgs.follows = "logos-module-builder/nixpkgs";
    nix-bundle-lgx.follows = "logos-module-builder/nix-bundle-lgx";
    delivery_module.url = "github:logos-co/logos-delivery-module/v0.1.1";
    delivery_module.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
