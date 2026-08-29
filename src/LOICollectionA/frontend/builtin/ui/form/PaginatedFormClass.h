#pragma once

#include <memory>
#include <string>
#include <vector>
#include <optional>

#include <ll/api/ui/form/CustomForm.h>

#include "LOICollectionA/frontend/AST.h"

namespace PaginatedFormClass {
    struct PaginatedFormHandle : LOICollection::frontend::NativeHandle {
        std::string guiId;
        ll::ui::TextValue title;

        std::vector<std::string> elements;

        int pageSize = 10;
        int page = 1;
        int pageCount = 1;

        std::vector<std::shared_ptr<ll::ui::ObservableString>> labels;
        std::vector<std::shared_ptr<ll::ui::ObservableBoolean>> visible;
        std::shared_ptr<ll::ui::ObservableString> pageIndicator;
        std::shared_ptr<ll::ui::ObservableString> input;
        std::shared_ptr<ll::ui::ObservableBoolean> previousVisible;
        std::shared_ptr<ll::ui::ObservableBoolean> nextVisible;
        std::shared_ptr<ll::ui::ObservableBoolean> chooseVisible;

        std::optional<ll::ui::TextValue> previousText;
        std::optional<ll::ui::TextValue> nextText;
        std::optional<ll::ui::TextValue> chooseText;
        std::optional<ll::ui::TextValue> choosePlaceholder;

        bool previousAdded = false;
        bool nextAdded = false;
        bool chooseAdded = false;
        bool closeButtonAdded = false;

        std::string selection;
        int selectionIndex = 0;
        
        int selectionPage = 0;

        LOICollection::frontend::FunctionRefPtr show;
        std::unique_ptr<ll::ui::CustomForm> base;
        std::string scriptId;

        void release() override {
            this->show.reset();
        }

        ~PaginatedFormHandle() override { this->release(); }
    };

    void refreshPage(const std::shared_ptr<PaginatedFormHandle>& form);
    void registerClasses(const std::string& name);
}
