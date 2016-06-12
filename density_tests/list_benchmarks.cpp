
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "../density/dense_list.h"
#include "../testity/test_tree.h"
#include <functional>
#include <vector>
#include <memory>

namespace density
{
    namespace tests
    {
        using namespace testity;

        PerformanceTestGroup make_list_benchmarks_1()
        {
            PerformanceTestGroup group("iterate dense_list and set variables", "density version: " + std::to_string(DENSITY_VERSION));

            using namespace std;

            struct Widget { int a, b, c, d, e, f, g, h; };
            struct TextWidget : Widget { char str[8]; };
            struct ImageWidget : Widget { float a, b, c; };

            static auto ptr_vector = []() {
                vector<unique_ptr<Widget>> res;
                for (size_t i = 0; i < 3000; i++)
                {
                    switch (i % 3)
                    {
                        case 0: res.push_back(make_unique<Widget>()); break;
                        case 1: res.push_back(make_unique<TextWidget>()); break;
                        case 2: res.push_back(make_unique<ImageWidget>()); break;
                        default: break;
                    }
                }
                return res;
            }();

            static auto den_list = []() {
                dense_list<Widget> list;
                for (size_t i = 0; i < 3000; i++)
                {
                    switch (i % 3)
                    {
                        case 0: list.push_back(Widget()); break;
                        case 1: list.push_back(TextWidget()); break;
                        case 2: list.push_back(ImageWidget()); break;
                        default: break;
                    }
                }
                return list;
            }();

            group.add_test(__FILE__, __LINE__, [](size_t i_cardinality) {
                for (size_t index = 0; index < i_cardinality; index += 20)
                {
                    for (const auto & wid : ptr_vector)
                    {
                        wid->b = wid->a = 0;
                        wid->d = wid->c = 0;
                        wid->e = 0;
                    }
                }
            }, __LINE__);

            group.add_test(__FILE__, __LINE__, [](size_t i_cardinality) {
                for (size_t index = 0; index < i_cardinality; index += 20)
                {
                    for (auto & wid : den_list)
                    {
                        wid.b = wid.a = 0;
                        wid.d = wid.c = 0;
                        wid.e = 0;
                    }
                }
            }, __LINE__);

            return group;
        }

        PerformanceTestGroup make_list_benchmarks_2()
        {
            PerformanceTestGroup group("iterate dense_list and call virtual func", "density version: " + std::to_string(DENSITY_VERSION));

            using namespace std;

            struct Widget { int var[8]; virtual void f() { memset(var, 0, sizeof(var) ); } };
            struct TextWidget : Widget { int var[3]; void f() { memset(var, 0, sizeof(var)); }  };
            struct ImageWidget : Widget { int var[8]; void f() { memset(var, 0, sizeof(var)); } };

            static auto ptr_vector = []() {
                vector<unique_ptr<Widget>> res;
                for (size_t i = 0; i < 3000; i++)
                {
                    switch (i % 3)
                    {
                    case 0: res.push_back(make_unique<Widget>()); break;
                    case 1: res.push_back(make_unique<TextWidget>()); break;
                    case 2: res.push_back(make_unique<ImageWidget>()); break;
                    default: break;
                    }
                }
                return res;
            }();

            static auto den_list = []() {
                dense_list<Widget> list;
                for (size_t i = 0; i < 3000; i++)
                {
                    switch (i % 3)
                    {
                    case 0: list.push_back(Widget()); break;
                    case 1: list.push_back(TextWidget()); break;
                    case 2: list.push_back(ImageWidget()); break;
                    default: break;
                    }
                }
                return list;
            }();

            group.add_test(__FILE__, __LINE__, [](size_t i_cardinality) {
                for (size_t index = 0; index < i_cardinality; index += 20)
                    for (const auto & wid : ptr_vector)
                    {
                        wid->f();
                    }
            }, __LINE__);

            group.add_test(__FILE__, __LINE__, [](size_t i_cardinality) {
                for (size_t index = 0; index < i_cardinality; index += 20)
                    for (auto & wid : den_list)
                    {
                        wid.f();
                    }
            }, __LINE__);

            return group;
        }

        void add_list_benchmarks(TestTree & i_tree)
        {
            i_tree.add_performance_test(make_list_benchmarks_1());
            i_tree.add_performance_test(make_list_benchmarks_2());
        }

    } // namespace tests

} // namespace density