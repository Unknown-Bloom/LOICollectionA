#pragma once

namespace LOICollection::server::Plugins {
    enum class MenuType {
        Simple,
        Modal,
        Custom
    };

    enum class MenuActionResult {
        Success,
        PermissionDenied,
        InsufficientScore
    };
}
