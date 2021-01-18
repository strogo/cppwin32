#pragma once

#include "type_writers.h"

#include <unordered_set>

namespace cppwin32
{
    struct separator
    {
        writer& w;
        bool first{ true };

        void operator()()
        {
            if (first)
            {
                first = false;
            }
            else
            {
                w.write(", ");
            }
        }
    };

    struct finish_with
    {
        writer& w;
        void (*finisher)(writer&);

        finish_with(writer& w, void (*finisher)(writer&)) : w(w), finisher(finisher) {}
        finish_with(finish_with const&) = delete;
        void operator=(finish_with const&) = delete;

        ~finish_with() { finisher(w); }
    };

    void write_include_guard(writer& w)
    {
        auto format = R"(#pragma once
)";

        w.write(format);
    }

    void write_close_namespace(writer& w)
    {
        auto format = R"(}
)";

        w.write(format);
    }

    [[nodiscard]] static finish_with wrap_impl_namespace(writer& w)
    {
        auto format = R"(namespace win32::_impl_
{
)";

        w.write(format);

        return { w, write_close_namespace };
    }

    [[nodiscard]] finish_with wrap_type_namespace(writer& w, std::string_view const& ns)
    {
        // TODO: Move into forwards
        auto format = R"(WIN32_EXPORT namespace win32::@
{
)";

        w.write(format, ns);

        return { w, write_close_namespace };
    }

    void write_enum_field(writer& w, Field const& field)
    {
        auto format = R"(        % = %,
)";

        if (auto constant = field.Constant())
        {
            w.write(format, field.Name(), *constant);
        }
    }

    void write_enum(writer& w, TypeDef const& type)
    {
        auto format = R"(    enum class % : %
    {
%    };
)";

        auto fields = type.FieldList();
        w.write(format, type.TypeName(), fields.first.Signature().Type(), bind_each<write_enum_field>(fields));
    }

    void write_forward(writer& w, TypeDef const& type)
    {
        std::string_view const type_keyword = is_union(type) ? "union" : "struct";
        auto format = R"(    % %;
)";

        w.write(format, type_keyword, type.TypeName());
    }

    struct struct_field
    {
        std::string_view name;
        std::string type;
        std::optional<int32_t> array_count;
    };

    void write_nesting(writer& w, int nest_level)
    {
        for (int i = 0; i < nest_level; ++i)
        {
            w.write("    ");
        }
    }

    void write_struct_field(writer& w, struct_field const& field, int nest_level = 0)
    {
        if (field.array_count)
        {
            w.write("        %@ %[%];\n",
                bind<write_nesting>(nest_level), field.type, field.name, field.array_count.value());
        }
        else
        {
            w.write("        %@ %;\n",
                bind<write_nesting>(nest_level), field.type, field.name);
        }
    }

    TypeDef get_nested_type(TypeSig const& type)
    {
        auto index = std::get_if<coded_index<TypeDefOrRef>>(&type.Type());
        TypeDef result{};
        if (index)
        {
            if (index->type() == TypeDefOrRef::TypeDef)
            {
                if (index->TypeDef().EnclosingType())
                {
                    result = index->TypeDef();
                }
            }
            else if (index->TypeRef().ResolutionScope().type() == ResolutionScope::TypeRef)
            {
                result = find(index->TypeRef());
            }
        }
        return result;
    }

    void write_struct(writer& w, TypeDef const& type, int nest_level = 0)
    {
#ifdef _DEBUG
        if (type.TypeName() == "EVENT_PROPERTY_INFO")
        {
            type.TypeNamespace();
        }
#endif

        std::string_view const type_keyword = is_union(type) ? "union" : "struct";
        w.write(R"(    %% %
    %{
)", bind<write_nesting>(nest_level), type_keyword, type.TypeName(), bind<write_nesting>(nest_level));

        // Write nested types
        for (auto&& nested_type : type.get_cache().nested_types(type))
        {
            write_struct(w, nested_type, nest_level + 1);
        }
        
        struct complex_struct
        {
            complex_struct(writer& w, TypeDef const& type)
                : type(type)
            {
                fields.reserve(size(type.FieldList()));
                for (auto&& field : type.FieldList())
                {
                    if (field.Flags().Literal())
                    {
                        continue;
                    }
                    auto const name = field.Name();
                    auto const signature = field.Signature();
                    auto const field_type = signature.Type();

                    std::optional<int32_t> array_count;
                    if (field_type.is_array())
                    {
                        XLANG_ASSERT(field_type.array_rank() == 1);
                        array_count = field_type.array_sizes()[0];
                    }
                    
                    //if (auto nested_type = get_nested_type(field_type))
                    //{
                    //    if (nested_type.Flags().Layout() == TypeLayout::ExplicitLayout && nested_type.TypeName().find("_e__Union") != std::string_view::npos)
                    //    {
                    //        // TODO: unions
                    //        continue;
                    //    }
                    //    else if (nested_type.TypeName().find("_e__Struct") != std::string_view::npos)
                    //    {
                    //        // TODO: unions
                    //        continue;
                    //    }
                    //    continue;
                    //}
                    //auto const index = std::get_if<coded_index<TypeDefOrRef>>(&field_type.Type());
                    //if (index && !find(*index))
                    //{
                    //    continue;
                    //}
                    fields.push_back({ name, w.write_temp("%", field_type), array_count });
                }
            }

            TypeDef type;
            std::vector<struct_field> fields;
        };

        complex_struct s{ w, type };

        for (auto&& field : s.fields)
        {
            write_struct_field(w, field, nest_level);
        }

        for (auto&& field : type.FieldList())
        {
            if (field.Flags().Literal())
            {
                auto const constant = field.Constant();
                w.write("        static constexpr % % = %;\n",
                    constant.Type(),
                    field.Name(),
                    constant);
            }
        }

        w.write(R"(    %};
)", bind<write_nesting>(nest_level));
    }

    MethodDef get_delegate_method(TypeDef const& type)
    {
        MethodDef invoke;
        for (auto&& method : type.MethodList())
        {
            if (method.Name() == "Invoke")
            {
                invoke = method;
                break;
            }
        }
        return invoke;
    }

    coded_index<TypeDefOrRef> get_base_interface(TypeDef const& type)
    {
        auto bases = type.InterfaceImpl();
        if (!empty(bases))
        {
            XLANG_ASSERT(size(bases) == 1);
            return bases.first.Interface();
        }
        return {};
    }

    struct dependency_sorter
    {
        struct node
        {
            std::vector<TypeDef> edges;
            bool temporary{};
            bool permanent{};

            // Number of edges on an individual node should be small, so linear search is fine.
            void add_edge(TypeDef const& edge)
            {
                if (std::find(edges.begin(), edges.end(), edge) == edges.end())
                {
                    edges.push_back(edge);
                }
            }
        };

        std::map<TypeDef, node> dependency_map;
        using value_type = std::map<TypeDef, node>::value_type;

        void add_struct(TypeDef const& type)
        {
#ifdef _DEBUG
            if (type.TypeName() == "DHCP_ALL_OPTIONS")
            {
                type.TypeNamespace();
            }
#endif
            auto [it, inserted] = dependency_map.insert({ type, {} });
            if (!inserted) return;
            for (auto&& field : type.FieldList())
            {
                auto const signature = field.Signature();
                if (auto const field_type = std::get_if<coded_index<TypeDefOrRef>>(&signature.Type().Type()))
                {
                    if (signature.Type().ptr_count() == 0 || is_nested(*field_type))
                    {
                        auto field_type_def = find(*field_type);
                        if (field_type_def && get_category(field_type_def) != category::enum_type)
                        {
                            it->second.add_edge(field_type_def);
                            add_struct(field_type_def);
                        }
                    }
                }
            }
        }

        void add_delegate(TypeDef const& type)
        {
            auto [it, inserted] = dependency_map.insert({ type, {} });
            if (!inserted) return;
            method_signature method_signature{ get_delegate_method(type) };
            auto add_param = [this, current = it](TypeSig const& type)
            {
                auto index = std::get_if<coded_index<TypeDefOrRef>>(&type.Type());
                if (index)
                {
                    auto param_type_def = find(*index);
                    if (param_type_def && get_category(param_type_def) == category::delegate_type)
                    {
                        current->second.add_edge(param_type_def);
                        add_delegate(param_type_def);
                    }
                }
            };
            add_param(method_signature.return_signature().Type());
            for (auto const& [param, param_sig] : method_signature.params())
            {
                add_param(param_sig->Type());
            }
        }

        void add_interface(TypeDef const& type)
        {
            auto [it, inserted] = dependency_map.insert({ type, {} });
            if (!inserted) return;

            auto const base_index = get_base_interface(type);
            if (base_index)
            {
                auto const base_type = find(base_index);
                if (base_type)
                {
                    it->second.add_edge(base_type);
                    add_interface(base_type);
                }
            }
        }

        void visit(value_type& v, std::vector<TypeDef>& sorted)
        {
#ifdef _DEBUG
            auto type_name = v.first.TypeName();
#endif

            if (v.second.permanent) return;
            if (v.second.temporary) throw std::invalid_argument("Cyclic dependency graph encountered");

            v.second.temporary = true;
            for (auto&& edge : v.second.edges)
            {
                auto it = dependency_map.find(edge);
                XLANG_ASSERT(it != dependency_map.end());
                visit(*it, sorted);
            }
            v.second.temporary = false;
            v.second.permanent = true;
            sorted.push_back(v.first);
        }

        std::vector<TypeDef> sort()
        {
            std::vector<TypeDef> result;
            auto eligible = [](value_type const& v) { return !v.second.permanent; };
            for (auto it = std::find_if(dependency_map.begin(), dependency_map.end(), eligible)
                ; it != dependency_map.end()
                ; it = std::find_if(dependency_map.begin(), dependency_map.end(), eligible))
            {
                visit(*it, result);
            }
            return result;
        }
    };

    void write_structs(writer& w, std::vector<TypeDef> const& structs)
    {
        dependency_sorter ds;
        for (auto&& type : structs)
        {
            ds.add_struct(type);
        }
        
        auto sorted_structs = ds.sort();
        for (auto&& type : sorted_structs)
        {
            if (get_category(type) == category::struct_type && !is_nested(type))
            {
                write_struct(w, type);
            }
        }
    }

    void write_abi_params(writer& w, method_signature const& method_signature)
    {
        separator s{ w };
        for (auto&& [param, param_signature] : method_signature.params())
        {
            s();
            std::string type;
            if (get_category(param_signature->Type()) == param_category::interface_type)
            {
                if (param.Flags().In())
                {
                    type = "void*";
                }
                else
                {
                    type = "void**";
                }
            }
            else
            {
                type = w.write_temp("%", param_signature->Type());
            }
            w.write("% %", type, param.Name());
        }
    }

    void write_abi_return(writer& w, RetTypeSig const& sig)
    {
        if (sig)
        {
            w.write(sig.Type());
        }
        else
        {
            w.write("void");
        }
    }

    int get_param_size(ParamSig const& param)
    {
        if (auto e = std::get_if<ElementType>(&param.Type().Type()))
        {
            if (param.Type().ptr_count() == 0)
            {
                switch (*e)
                {
                case ElementType::U8:
                case ElementType::I8:
                case ElementType::R8:
                    return 8;

                default:
                    return 4;
                }
            }
            else
            {
                return 4;
            }
        }
        else
        {
            return 4;
        }
    }

    void write_abi_link(writer& w, method_signature const& method_signature)
    {
        int count = 0;
        for (auto&& [param, param_signature] : method_signature.params())
        {
            count += get_param_size(*param_signature);
        }
        w.write("%, %", method_signature.method().Name(), count);
    }

    void write_consume_return_type(writer& w, method_signature const& signature)
    {
        if (!signature.return_signature())
        {
            return;
        }

        w.write("auto % = ", signature.return_param_name());
    }

    void write_consume_return_statement(writer& w, method_signature const& signature)
    {
        if (!signature.return_signature())
        {
            return;
        }

        TypeDef type;
        auto const category = get_category(signature.return_signature().Type(), &type);

        //if (category == param_category::interface_type)
        //{
        //    auto consume_guard = w.push_consume_types(true);
        //    w.write("\n        return com_ptr<%>{ %, take_ownership_from_abi };", signature.return_signature(), signature.return_param_name());
        //}
        //else
        {
            w.write("\n        return %;", signature.return_param_name());
        }
    }

    void write_class_abi(writer& w, TypeDef const& type)
    {
        auto abi_guard = w.push_abi_types(true);
        auto ns_guard = w.push_full_namespace(true);

        w.write(R"(extern "C"
{
)");
        auto const format = R"xyz(    % __stdcall WIN32_IMPL_%(%) noexcept;
)xyz";

        for (auto&& method : type.MethodList())
        {
            if (method.Flags().Access() == MemberAccess::Public)
            {
                method_signature signature{ method };
                w.write(format, bind<write_abi_return>(signature.return_signature()), method.Name(), bind<write_abi_params>(signature));
            }
        }
        w.write(R"(}
)");

        for (auto&& method : type.MethodList())
        {
            if (method.Flags().Access() == MemberAccess::Public)
            {
                method_signature signature{ method };
                w.write("WIN32_IMPL_LINK(%)\n", bind<write_abi_link>(signature));
            }
        }
        w.write("\n");
    }
    
    void write_method_params(writer& w, method_signature const& method_signature)
    {
        separator s{ w };
        for (auto&& [param, param_signature] : method_signature.params())
        {
            s();
            std::string type;
            TypeDef signature_type;
            auto category = get_category(param_signature->Type(), &signature_type);

            if (param.Flags().In())
            {
                switch (category)
                {
                case param_category::interface_type:
                {
                    auto guard = w.push_consume_types(true);
                    type = w.write_temp("% const&", param_signature->Type());
                }
                    break;

                default:
                    type = w.write_temp("%", param_signature->Type());
                    break;
                }
            }
            else
            {
                switch (category)
                {
                case param_category::interface_type:
                {
                    auto guard = w.push_consume_types(true);
                    type = w.write_temp("%&", param_signature->Type());
                }
                break;

                default:
                    type = w.write_temp("%", param_signature->Type());
                    break;
                }
            }
            w.write("% %", type, param.Name());
        }
    }

    void write_method_args(writer& w, method_signature const& method_signature)
    {
        separator s{ w };
        for (auto&& [param, param_signature] : method_signature.params())
        {
            s();
            auto const param_name = param.Name();

            TypeDef signature_type;
            auto category = get_category(param_signature->Type(), &signature_type);

            if (param.Flags().In())
            {
                switch (category)
                {
                case param_category::interface_type:
                    w.write("*(void**)(&%)", param_name);
                    break;
                default:
                    w.write(param_name);
                    break;
                }
            }
            else
            {
                switch (category)
                {
                case param_category::interface_type:
                    w.write("_impl_::bind_out(%)", param_name);
                    break;
                default:
                    w.write(param_name);
                    break;
                }
            }
        }
    }

    void write_method_return(writer& w, method_signature const& method_signature)
    {
        auto const& ret = method_signature.return_signature();
        if (ret)
        {
            auto const category = get_category(ret.Type());
            if (category == param_category::interface_type)
            {
                auto consume_guard = w.push_consume_types(true);
                w.write(ret.Type());
            }
            else
            {
                w.write(ret.Type());

            }
        }
        else
        {
            w.write("void");
        }
    }

    void write_class_method(writer& w, method_signature const& method_signature)
    {
        auto const format = R"xyz(    %% %(%)
    {
        %WIN32_IMPL_%(%);%
    }
)xyz";
        std::string_view modifier;
        if (method_signature.method().Flags().Static())
        {
            modifier = "static ";
        }

        w.write(format,
            modifier,
            bind<write_method_return>(method_signature),
            method_signature.method().Name(),
            bind<write_method_params>(method_signature),
            bind<write_consume_return_type>(method_signature),
            method_signature.method().Name(),
            bind<write_method_args>(method_signature),
            bind<write_consume_return_statement>(method_signature)
        );
    }

    void write_class(writer& w, TypeDef const& type)
    {
        {
            auto const format = R"(    struct %
    {
)";
            w.write(format, type.TypeName());
        }
        for (auto&& method : type.MethodList())
        {
            if (method.Flags().Access() == MemberAccess::Public)
            {
                method_signature signature{ method };
                write_class_method(w, signature);
            }
        }

        w.write("\n");

        for (auto&& field : type.FieldList())
        {
            if (field.Flags().Literal())
            {
                auto const constant = field.Constant();
                w.write("        static constexpr % % = %;\n",
                    constant.Type(),
                    field.Name(),
                    constant);
            }
        }
        w.write(R"(
    };
)");
    }

    void write_delegate_params(writer& w, method_signature const& method_signature)
    {
        separator s{ w };
        for (auto&& [param, param_signature] : method_signature.params())
        {
            s();
            std::string type;
            type = w.write_temp("%", param_signature->Type());
            w.write("%", type);
        }
    }

    void write_delegate(writer& w, TypeDef const& type)
    {
        auto const format = R"xyz(    using % = std::add_pointer_t<% __stdcall(%)>;
)xyz";
        method_signature method_signature{ get_delegate_method(type) };

        w.write(format, type.TypeName(), bind<write_method_return>(method_signature), bind<write_delegate_params>(method_signature));
    }

    void write_delegates(writer& w, std::vector<TypeDef> const& delegates)
    {
        dependency_sorter ds;
        for (auto&& type : delegates)
        {
            ds.add_delegate(type);
        }

        auto sorted_delegates = ds.sort();
        for (auto&& type : sorted_delegates)
        {
            if (get_category(type) == category::delegate_type)
            {
                write_delegate(w, type);
            }
        }
    }

    void write_enum_operators(writer& w, TypeDef const& type)
    {
        if (!get_attribute(type, "System", "FlagsAttribute"))
        {
            return;
        }

        auto name = type.TypeName();

        auto format = R"(    constexpr auto operator|(% const left, % const right) noexcept
    {
        return static_cast<%>(_impl_::to_underlying_type(left) | _impl_::to_underlying_type(right));
    }
    constexpr auto operator|=(%& left, % const right) noexcept
    {
        left = left | right;
        return left;
    }
    constexpr auto operator&(% const left, % const right) noexcept
    {
        return static_cast<%>(_impl_::to_underlying_type(left) & _impl_::to_underlying_type(right));
    }
    constexpr auto operator&=(%& left, % const right) noexcept
    {
        left = left & right;
        return left;
    }
    constexpr auto operator~(% const value) noexcept
    {
        return static_cast<%>(~_impl_::to_underlying_type(value));
    }
    constexpr auto operator^^(% const left, % const right) noexcept
    {
        return static_cast<%>(_impl_::to_underlying_type(left) ^^ _impl_::to_underlying_type(right));
    }
    constexpr auto operator^^=(%& left, % const right) noexcept
    {
        left = left ^^ right;
        return left;
    }
)";
        w.write(format, name, name, name, name, name, name, name, name, name, name, name, name, name, name, name, name, name);
    }

    struct guid
    {
        uint32_t Data1;
        uint16_t Data2;
        uint16_t Data3;
        uint8_t  Data4[8];
    };

    guid to_guid(std::string_view const& str)
    {
        if (str.size() < 36)
        {
            throw_invalid("Invalid GuidAttribute blob");
        }
        guid result;
        auto const data = str.data();
        std::from_chars(data,      data + 8,  result.Data1, 16);
        std::from_chars(data + 9,  data + 13, result.Data2, 16);
        std::from_chars(data + 14, data + 18, result.Data3, 16);
        std::from_chars(data + 19, data + 21, result.Data4[0], 16);
        std::from_chars(data + 21, data + 23, result.Data4[1], 16);
        std::from_chars(data + 24, data + 26, result.Data4[2], 16);
        std::from_chars(data + 26, data + 28, result.Data4[3], 16);
        std::from_chars(data + 28, data + 30, result.Data4[4], 16);
        std::from_chars(data + 30, data + 32, result.Data4[5], 16);
        std::from_chars(data + 32, data + 34, result.Data4[6], 16);
        std::from_chars(data + 34, data + 36, result.Data4[7], 16);
        return result;
    }

    void write_guid_value(writer& w, guid const& g)
    {
        w.write_printf("0x%08X,0x%04X,0x%04X,{ 0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X }",
            g.Data1,
            g.Data2,
            g.Data3,
            g.Data4[0],
            g.Data4[1],
            g.Data4[2],
            g.Data4[3],
            g.Data4[4],
            g.Data4[5],
            g.Data4[6],
            g.Data4[7]);
    }

    void write_guid(writer& w, TypeDef const& type)
    {
        auto const name = type.TypeName();
        if (name == "IUnknown")
        {
            return;
        }
        auto attribute = get_attribute(type, "System.Runtime.InteropServices", "GuidAttribute");
        if (!attribute)
        {
            return;
        }

        auto const sig = attribute.Value();
        auto const guid_str = std::get<std::string_view>(std::get<ElemSig>(sig.FixedArgs()[0].value).value);
        auto const guid_value = to_guid(guid_str);

        auto format = R"(    template <> inline constexpr guid guid_v<%>{ % }; // %
)";

        w.write(format,
            type,
            bind<write_guid_value>(guid_value),
            guid_str);
    }

    void write_base_interface(writer& w, TypeDef const& type)
    {
        auto const base = get_base_interface(type);
        if (base)
        {
            w.write(" : %", base);
        }
    }

    void write_interface(writer& w, TypeDef const& type)
    {
        {
            auto const format = R"(    struct __declspec(novtable) %%
    {
)";
            w.write(format, type.TypeName(), bind<write_base_interface>(type));
        }

        auto const format = R"(        virtual % __stdcall %(%) noexcept = 0;
)";
        auto abi_guard = w.push_abi_types(true);
        
        for (auto&& method : type.MethodList())
        {
            method_signature signature{ method };
            w.write(format, bind<write_abi_return>(signature.return_signature()), method.Name(), bind<write_abi_params>(signature));
        }

        w.write(R"(    };
)");
    }

    void write_interfaces(writer& w, std::vector<TypeDef> const& interfaces)
    {
        dependency_sorter ds;
        for (auto&& type : interfaces)
        {
            ds.add_interface(type);
        }

        auto sorted_types = ds.sort();
        for (auto&& type : sorted_types)
        {
            write_interface(w, type);
        }
    }

    void write_consume_params(writer& w, method_signature const& signature)
    {
        write_method_params(w, signature);
    }

    void write_consume_declaration(writer& w, MethodDef const& method)
    {
        method_signature signature{ method };

        auto const name = method.Name();
        w.write("        WIN32_IMPL_AUTO(%) %(%) const;\n",
            signature.return_signature(),
            name,
            bind<write_consume_params>(signature));
    }

    void write_consume(writer& w, TypeDef const& type)
    {
        auto const& method_list = type.MethodList();
        auto const impl_name = get_impl_name(type.TypeNamespace(), type.TypeName());

        auto const format = R"(    struct consume_%
    {
%    };
)";

        w.write(format,
            impl_name,
            bind_each<write_consume_declaration>(method_list));
    }

    void write_raii_helper(writer& w, Param const& param, std::set<std::string_view>& helpers)
    {
        auto const attr = get_attribute(param, "Windows.Win32.Interop", "RAIIFreeAttribute");
        if (!attr)
        {
            return;
        }
        auto const attr_sig = attr.Value();
        auto const function_name = std::get<std::string_view>(std::get<ElemSig>(attr_sig.FixedArgs()[0].value).value);

        auto const [iter, inserted] = helpers.insert(function_name);
        if (!inserted)
        {
            return;
        }

        auto const apis = param.get_cache().find_required("Windows.Win32", "Apis");
        auto const methods = apis.MethodList();
        auto const function = std::find_if(methods.first, methods.second,
            [&function_name](MethodDef const& method)
            {
                return method.Name() == function_name;
            });

        method_signature signature(function);

        w.write("    using unique_% = unique_any<%, decltype(&WIN32_IMPL_%), WIN32_IMPL_%>;\n",
            function_name,
            signature.params()[0].second->Type(),
            function_name,
            function_name);
    }

    void write_method_raii_helpers(writer& w, MethodDef const& method, std::set<std::string_view>& helpers)
    {
        for (auto&& param : method.ParamList())
        {
            write_raii_helper(w, param, helpers);
        }
    }

    void write_api_raii_helpers(writer& w, TypeDef const& type, std::set<std::string_view>& helpers)
    {
        for (auto&& method : type.MethodList())
        {
            write_method_raii_helpers(w, method, helpers);
        }
    }

    void write_consume_definition(writer& w, TypeDef const& type, MethodDef const& method, std::string_view const& type_impl_name)
    {
        auto const method_name = method.Name();
        auto signature = method_signature(method);

        auto const format = R"(    WIN32_IMPL_AUTO(%) consume_%::%(%) const
    {
        %WIN32_IMPL_SHIM(%)->%(%);%
    }
)";

        w.write(format,
            signature.return_signature(),
            type_impl_name,
            method_name,
            bind<write_consume_params>(signature),
            bind<write_consume_return_type>(signature),
            type,
            method_name,
            bind<write_method_args>(signature),
            bind<write_consume_return_statement>(signature)
        );
    }
}
