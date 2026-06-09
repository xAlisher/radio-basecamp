{
  description = "radio_module — decentralized audio broadcast origin + discovery (core module)";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    # Pin nixpkgs/bundler to the builder's so the Rust/zerokit toolchain that delivery_module
    # pulls (RLN) resolves from the shared binary cache instead of building from source.
    nixpkgs.follows = "logos-module-builder/nixpkgs";
    nix-bundle-lgx.follows = "logos-module-builder/nix-bundle-lgx";
    # delivery_module is auto-resolved from metadata.json `dependencies` via this input.
    # Input attr name MUST match the dependency name. Pinned to v0.1.1 (proven in scorched-earth).
    delivery_module.url = "github:logos-co/logos-delivery-module/v0.1.1";
    delivery_module.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
