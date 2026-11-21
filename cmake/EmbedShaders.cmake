string(REPLACE "|" ";" sources "${SHADER_SOURCES}")
set(content "")
foreach(source IN LISTS sources)
    file(READ "${source}" text)
    string(APPEND content "${text}\n")
endforeach()
file(WRITE "${OUTPUT}.tmp"
    "#include \"render/ShaderLibrary.h\"\n\nnamespace prism::render {\nstd::string_view metalShaderSource() noexcept {\n    static constexpr char source[] = R\"prism(${content})prism\";\n    return {source, sizeof(source) - 1};\n}\n}\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
