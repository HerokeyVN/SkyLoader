# OverlaySession registry

## Context

Bootstrap currently keeps Vulkan overlay state in one active session. Swapchain
recreation or a second swapchain can replace that state while a present still
references it.

## Decision

OverlayRuntime owns OverlaySession instances keyed by `(VkDevice,
VkSwapchainKHR)`. Vulkan hooks create, find, and destroy only the matching
session. A present without a matching ready session forwards unchanged.

## Consequences

- Resize and recreation have one resource-lifetime owner.
- Overlay failure is fail-open for only the affected session.
- Plugin lifecycle is session-scoped; a recreated session must initialize
  ready plugins again.
