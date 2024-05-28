"""
A custom made antlr rule to generate C++ runtime files directly from the grammar file
"""

def antlr_cc_library(name, lexer_src, parser_src, package):
    """Creates a C++ lexer and parser from a source grammar.

    Args:
        name: Base name for the lexer and the parser rules.
        lexer_src: source ANTLR lexer file
        parser_src: source ANTLR parser file
        package: The namespace for the generated code
    """
    generated_lexer = name + "_lexer"

    antlr_lexer_library(
        name = generated_lexer,
        src = lexer_src,
        package = package,
    )

    generated_parser = name + "_parser"

    antlr_parser_library(
        name = generated_parser,
        lexer = generated_lexer,
        src = parser_src,
        package = package,
    )

    native.cc_library(
        name = name + "_cc_parser",
        srcs = [generated_lexer, generated_parser],
        deps = [
            generated_lexer,
            generated_parser,
            "@antlr4-cpp-runtime//:antlr4-cpp-runtime",
        ],
        linkstatic = 1,
    )

def _antlr_lexer_library(ctx):
    output_files = []

    for extension in [".h", ".cpp", ".tokens"]:
        output_file = ctx.actions.declare_file(ctx.attr.name + "/" + ctx.file.src.path[:-3] + extension)
        output_files.append(output_file)

    output_dir = output_files[0].path[:output_files[0].path.rfind(ctx.file.src.path[:-3])]

    antlr_args = ctx.actions.args()
    antlr_args.add("-no-listener")
    antlr_args.add("-visitor")
    antlr_args.add("-o", output_dir)
    antlr_args.add("-package", ctx.attr.package)
    antlr_args.add(ctx.file.src)

    ctx.actions.run(
        mnemonic = "AnlrLexerGeneration",
        arguments = [antlr_args],
        inputs = [ctx.file.src],
        outputs = output_files,
        executable = ctx.executable._tool,
        progress_message = "Processing ANTLR lexer tokens",
    )

    copied_files = []

    for output in output_files:

        copied = ctx.actions.declare_file("parser/generated/" + output.basename)

        ctx.actions.run_shell(
            mnemonic = "CopyFile",
            inputs = [output],
            outputs = [copied],
            command = 'cp "{generated}" "{out}"'.format(generated = output.path, out = copied.path),
        )

        copied_files.append(copied)

    compilation_context = cc_common.create_compilation_context(headers = depset(copied_files))
    return [DefaultInfo(files = depset(copied_files)), CcInfo(compilation_context = compilation_context)]

def _antlr_parser_library(ctx):
    lexer_files = ctx.attr.lexer[DefaultInfo].files.to_list()

    tokens_files = [f for f in lexer_files if f.basename.endswith(".tokens")]

    if not tokens_files:
        fail("No tokens file found in lexer output")

    output_files = []

    for suffix in ["", "BaseVisitor", "Visitor"]:
        for extension in [".h", ".cpp"]:
            output_file = ctx.actions.declare_file(ctx.attr.name + "/" + ctx.file.src.path[:-3] + suffix + extension)
            output_files.append(output_file)

    output_dir = output_files[0].path[:output_files[0].path.rfind(ctx.file.src.path[:-3])]

    antlr_args = ctx.actions.args()
    antlr_args.add("-no-listener")
    antlr_args.add("-visitor")
    antlr_args.add("-o", output_dir)
    antlr_args.add("-package", ctx.attr.package)
    antlr_args.add("-lib", tokens_files[0].dirname)
    antlr_args.add(ctx.file.src)

    ctx.actions.run(
        arguments = [antlr_args],
        inputs = [ctx.file.src, tokens_files[0]],
        outputs = output_files,
        executable = ctx.executable._tool,
        progress_message = "Processing ANTLR lexer tokens",
    )

    copied_files = []

    for output in output_files:

        copied = ctx.actions.declare_file("parser/generated/" + output.basename)

        ctx.actions.run_shell(
            mnemonic = "CopyFile",
            inputs = [output],
            outputs = [copied],
            command = 'cp "{generated}" "{out}"'.format(generated = output.path, out = copied.path),
        )

        copied_files.append(copied)

    compilation_context = cc_common.create_compilation_context(headers = depset(copied_files))
    return [DefaultInfo(files = depset(copied_files)), CcInfo(compilation_context = compilation_context)]

antlr_lexer_library = rule(
    implementation = _antlr_lexer_library,
    attrs = {
        "src": attr.label(allow_single_file = [".g4"], mandatory = True),
        "package": attr.string(),
        "_tool": attr.label(
            executable = True,
            cfg = "exec",  # buildifier: disable=attr-cfg
            default = Label("//bazel:antlr4_tool"),
        ),
    },
)

antlr_parser_library = rule(
    implementation = _antlr_parser_library,
    attrs = {
        "src": attr.label(allow_single_file = [".g4"], mandatory = True),
        "lexer": attr.label(mandatory = True),
        "package": attr.string(),
        "_tool": attr.label(
            executable = True,
            cfg = "exec",  # buildifier: disable=attr-cfg
            default = Label("//bazel:antlr4_tool"),
        ),
    },
)
