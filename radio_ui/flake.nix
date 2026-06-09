{
  description = "radio_ui — two-tab Stream/Listen UI for radio_module (QML-only module)";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    # radio_module is auto-resolved from metadata.json `dependencies`; attr name must match.
    radio_module.url = "path:../radio_module";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
