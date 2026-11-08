#include "Run.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>


int main(int argc, char** argv)
{
    namespace Launcher = Radiocom::Launcher;

    const std::span<char*> args(argv, size_t(argc));
    const auto options = Launcher::parseCommandLine(args);
    if (!options.has_value())
    {
        Launcher::printUsage(args[0]);
        return EXIT_FAILURE;
    }
    if (options->isHelpRequested)
    {
        Launcher::printUsage(args[0]);
        return EXIT_SUCCESS;
    }

    try
    {
        return Launcher::run(*options);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Terminated by an unhandled exception: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
