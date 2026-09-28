cmake_minimum_required(VERSION 3.13)

set(MODULE_NAME         cli_parser)
project(                ${MODULE_NAME})

# ObjectHandler comes first, so it inherits none of the parser's include paths or definitions.
add_subdirectory(source/object_handler)
enable_testing()
add_subdirectory(tests/object_handler)

add_definitions(-DUNIX)

set(INCLUDE_DIRS        ./
                        ./source
                        ./source/row_table
                        ./source/methods
                        )

set(SOURCE_FILES        ./source/main.c
                        ./source/pe_parser.c
                        ./source/ecma_335_parser.c
                        ./source/metadata_table.c

                        ./source/methods/method_parser.c
                        ./source/methods/signature_parser.c
                        ./source/methods/typedef_parser.c
                        ./source/methods/fields_parser.c
                        ./source/methods/type_ref_parser.c
                        ./source/methods/member_ref_parser.c

                        ./source/framework/pool.c
                        ./source/framework/critical_section.c

                        ./source/row_table/module_table.c
                        ./source/row_table/typeref_table.c
                        ./source/row_table/typedef_table.c
                        ./source/row_table/field_table.c
                        ./source/row_table/methoddef_table.c
                        ./source/row_table/param_table.c
                        ./source/row_table/interfaceimpl_table.c
                        ./source/row_table/memberref_table.c
                        ./source/row_table/constant_table.c
                        ./source/row_table/customattribute_table.c
                        ./source/row_table/classlayout_table.c
                        ./source/row_table/fieldlayout_table.c
                        ./source/row_table/standalonesig_table.c
                        ./source/row_table/eventmap_table.c
                        ./source/row_table/event_table.c
                        ./source/row_table/propertymap_table.c
                        ./source/row_table/property_table.c
                        ./source/row_table/methodsemantics_table.c
                        ./source/row_table/methodimpl_table.c
                        ./source/row_table/moduleref_table.c
                        ./source/row_table/typespec_table.c
                        ./source/row_table/implmap_table.c
                        ./source/row_table/fieldrva_table.c
                        ./source/row_table/fieldmarshal_table.c
                        ./source/row_table/declsecurity_table.c
                        ./source/row_table/assembly_table.c
                        ./source/row_table/assemblyref_table.c
                        ./source/row_table/file_table.c
                        ./source/row_table/exportedtype_table.c
                        ./source/row_table/manifestresource_table.c
                        ./source/row_table/nestedclass_table.c
                        ./source/row_table/genericparam_table.c
                        ./source/row_table/methodspec_table.c
                        ./source/row_table/genericparamconstraint_table.c
                        )

include_directories(    ${INCLUDE_DIRS} )

add_executable(         ${MODULE_NAME}
                        ${SOURCE_FILES})

#add_dependencies(       ${MODULE_NAME}
#                        ${RTL_BLOCK_BUILD}
#                        )

#target_link_libraries(  ${MODULE_NAME}
#                        ${RTL_LIBRARIES} )

