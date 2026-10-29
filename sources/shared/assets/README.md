# Shared assets

`default_launcher.html` is the canonical cross-platform launcher template. Platform-specific build systems should generate their launcher copy from this file when their project configuration requests a generated/default launcher, substituting `{{APP_NAME}}` and `{{WEBPAGE}}` safely for HTML context. Do not create per-console template forks.

At present, the Vita build consumes this template when its `launchPage` setting is empty. Wii and Wii U still use their configured webpage entry point directly; extending launcher generation to those ports requires wiring their manifest/staging conventions rather than duplicating the template.
