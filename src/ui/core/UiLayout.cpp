#include "UiLayout.h"

#include <algorithm>

namespace sdlviewer
{
    namespace UiLayout
    {
        namespace
        {
            struct Bucket
            {
                std::vector<UiComponentBase *> children;
            };

            float NonNegative(float value)
            {
                return std::max(0.0f, value);
            }

            float Spacing(size_t count, float inspace)
            {
                return count > 1 ? static_cast<float>(count - 1) * inspace : 0.0f;
            }

            AlignType ChildAlign(const UiComponentBase &child, const WidgetConfig &container)
            {
                const WidgetConfig *childOpt = child.GetOpt();
                if (childOpt != nullptr && childOpt->Align != AlignType::None)
                {
                    return childOpt->Align;
                }

                return container.Align != AlignType::None ? container.Align : AlignType::Left;
            }

            float BucketWidth(const Bucket &bucket, float inspace)
            {
                float width = Spacing(bucket.children.size(), inspace);
                for (const UiComponentBase *child : bucket.children)
                {
                    width += child->GetPreferredSize().x;
                }
                return width;
            }

            int ExpandingWidthCount(const Bucket &bucket)
            {
                int count = 0;
                for (const UiComponentBase *child : bucket.children)
                {
                    const WidgetConfig *childOpt = child->GetOpt();
                    if (childOpt != nullptr && childOpt->scale.width == ScaleType::Expanding)
                    {
                        ++count;
                    }
                }
                return count;
            }

            void ArrangeBucket(
                const Bucket &bucket,
                AlignType align,
                float regionLeft,
                float regionWidth,
                float contentTop,
                float contentHeight,
                float inspace)
            {
                if (bucket.children.empty())
                {
                    return;
                }

                const float preferredWidth = BucketWidth(bucket, inspace);
                const int expandingCount = ExpandingWidthCount(bucket);
                const float extra = NonNegative(regionWidth - preferredWidth);
                const float extraPerChild = expandingCount > 0
                                                ? extra / static_cast<float>(expandingCount)
                                                : 0.0f;
                const float usedWidth = preferredWidth + extraPerChild * static_cast<float>(expandingCount);

                float cursorX = regionLeft;
                if (align == AlignType::Center)
                {
                    cursorX += NonNegative(regionWidth - usedWidth) * 0.5f;
                }
                else if (align == AlignType::Right)
                {
                    cursorX += NonNegative(regionWidth - usedWidth);
                }

                for (UiComponentBase *child : bucket.children)
                {
                    const WidgetConfig *childOpt = child->GetOpt();
                    ImVec2 childSize = child->GetPreferredSize();

                    if (childOpt != nullptr && childOpt->scale.width == ScaleType::Expanding)
                    {
                        childSize.x += extraPerChild;
                    }
                    if (childOpt != nullptr && childOpt->scale.height == ScaleType::Expanding)
                    {
                        childSize.y = contentHeight;
                    }

                    const float childY = contentTop + NonNegative(contentHeight - childSize.y) * 0.5f;
                    child->Arrange(cursorX, childY, childSize.x, childSize.y);
                    cursorX += childSize.x + inspace;
                }
            }

            void ArrangeHorizontal(
                const std::vector<std::unique_ptr<UiComponentBase>> &children,
                const WidgetConfig &container,
                float contentLeft,
                float contentTop,
                float contentWidth,
                float contentHeight)
            {
                Bucket left;
                Bucket center;
                Bucket right;

                for (const auto &child : children)
                {
                    if (!child)
                    {
                        continue;
                    }

                    if (child->IsHidden())
                    {
                        continue;
                    }

                    switch (ChildAlign(*child, container))
                    {
                    case AlignType::Center:
                        center.children.push_back(child.get());
                        break;
                    case AlignType::Right:
                        right.children.push_back(child.get());
                        break;
                    default:
                        left.children.push_back(child.get());
                        break;
                    }
                }

                float leftRegionWidth = BucketWidth(left, container.inspace);
                float rightRegionWidth = BucketWidth(right, container.inspace);
                const float leftCenterGap = !left.children.empty() && !center.children.empty()
                                                ? container.inspace
                                                : 0.0f;
                const float centerRightGap = !center.children.empty() && !right.children.empty()
                                                 ? container.inspace
                                                 : 0.0f;
                const float leftRightGap = center.children.empty() && !left.children.empty() && !right.children.empty()
                                               ? container.inspace
                                               : 0.0f;

                if (center.children.empty())
                {
                    const int leftExpand = ExpandingWidthCount(left);
                    const int rightExpand = ExpandingWidthCount(right);
                    const int expandCount = leftExpand + rightExpand;
                    const float freeWidth = NonNegative(
                        contentWidth - leftRegionWidth - rightRegionWidth - leftRightGap);

                    if (expandCount > 0)
                    {
                        leftRegionWidth += freeWidth * static_cast<float>(leftExpand) / static_cast<float>(expandCount);
                        rightRegionWidth += freeWidth * static_cast<float>(rightExpand) / static_cast<float>(expandCount);
                    }
                }

                const float rightRegionLeft = contentLeft + contentWidth - rightRegionWidth;
                ArrangeBucket(left, AlignType::Left, contentLeft, leftRegionWidth,
                              contentTop, contentHeight, container.inspace);
                ArrangeBucket(right, AlignType::Right, rightRegionLeft, rightRegionWidth,
                              contentTop, contentHeight, container.inspace);

                if (!center.children.empty())
                {
                    const float centerLeft = contentLeft + leftRegionWidth + leftCenterGap;
                    const float centerRight = rightRegionLeft - centerRightGap;
                    ArrangeBucket(center, AlignType::Center, centerLeft,
                                  NonNegative(centerRight - centerLeft), contentTop, contentHeight,
                                  container.inspace);
                }
            }

            void ArrangeVertical(
                const std::vector<std::unique_ptr<UiComponentBase>> &children,
                const WidgetConfig &container,
                float contentLeft,
                float contentTop,
                float contentWidth,
                float contentHeight)
            {
                float preferredHeight = Spacing(children.size(), container.inspace);
                int expandingCount = 0;

                for (const auto &child : children)
                {
                    if (!child)
                    {
                        continue;
                    }

                    if (child->IsHidden())
                    {
                        continue;
                    }

                    preferredHeight += child->GetPreferredSize().y;
                    const WidgetConfig *childOpt = child->GetOpt();
                    if (childOpt != nullptr && childOpt->scale.height == ScaleType::Expanding)
                    {
                        ++expandingCount;
                    }
                }

                const float extraHeight = NonNegative(contentHeight - preferredHeight);
                const float extraPerChild = expandingCount > 0
                                                ? extraHeight / static_cast<float>(expandingCount)
                                                : 0.0f;
                float cursorY = contentTop;

                for (const auto &child : children)
                {
                    if (!child)
                    {
                        continue;
                    }

                    if (child->IsHidden())
                    {
                        continue;
                    }

                    const WidgetConfig *childOpt = child->GetOpt();
                    ImVec2 childSize = child->GetPreferredSize();
                    if (childOpt != nullptr && childOpt->scale.height == ScaleType::Expanding)
                    {
                        childSize.y += extraPerChild;
                    }
                    if (childOpt != nullptr && childOpt->scale.width == ScaleType::Expanding)
                    {
                        childSize.x = contentWidth;
                    }

                    float childX = contentLeft;
                    switch (ChildAlign(*child, container))
                    {
                    case AlignType::Center:
                        childX += NonNegative(contentWidth - childSize.x) * 0.5f;
                        break;
                    case AlignType::Right:
                        childX += NonNegative(contentWidth - childSize.x);
                        break;
                    default:
                        break;
                    }

                    child->Arrange(childX, cursorY, childSize.x, childSize.y);
                    cursorY += childSize.y + container.inspace;
                }
            }
        } // namespace

        LayoutType ResolveLayoutType(const WidgetConfig &container)
        {
            if (container.LayoutMod != LayoutType::Auto)
            {
                return container.LayoutMod;
            }

            return container.scale.width == ScaleType::Expanding && container.scale.height == ScaleType::Fixed
                       ? LayoutType::Horizontal
                       : LayoutType::Vertical;
        }

        ImVec2 MeasureChildren(
            const std::vector<std::unique_ptr<UiComponentBase>> &children,
            const WidgetConfig &container)
        {
            float width = 0.0f;
            float height = 0.0f;

            if (ResolveLayoutType(container) == LayoutType::Horizontal)
            {
                for (const auto &child : children)
                {
                    if (!child)
                        continue;
                    const ImVec2 preferred = child->GetPreferredSize();
                    width += preferred.x;
                    height = std::max(height, preferred.y);
                }
                width += Spacing(children.size(), container.inspace);
            }
            else
            {
                for (const auto &child : children)
                {
                    if (!child)
                        continue;
                    const ImVec2 preferred = child->GetPreferredSize();
                    width = std::max(width, preferred.x);
                    height += preferred.y;
                }
                height += Spacing(children.size(), container.inspace);
            }

            return ImVec2(
                container.margin.left + width + container.margin.right,
                container.margin.top + height + container.margin.bottom);
        }

        void ArrangeChildren(
            const std::vector<std::unique_ptr<UiComponentBase>> &children,
            const WidgetConfig &container,
            const ImVec2 &containerSize)
        {
            const float left = container.margin.left;
            const float top = container.margin.top;
            const float width = NonNegative(containerSize.x - container.margin.left - container.margin.right);
            const float height = NonNegative(containerSize.y - container.margin.top - container.margin.bottom);

            if (ResolveLayoutType(container) == LayoutType::Horizontal)
            {
                ArrangeHorizontal(children, container, left, top, width, height);
            }
            else
            {
                ArrangeVertical(children, container, left, top, width, height);
            }
        }
    } // namespace UiLayout
} // namespace sdlviewer
