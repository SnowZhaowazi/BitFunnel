// The MIT License (MIT)

// Copyright (c) 2016, Microsoft

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "gtest/gtest.h"

#include "BitFunnel/Configuration/Factories.h"
#include "BitFunnel/Configuration/IStreamConfiguration.h"
#include "BitFunnel/Plan/QueryParser.h"
#include "BitFunnel/Plan/TermMatchNode.h"
#include "BitFunnel/Utilities/Allocator.h"
#include "BitFunnel/Utilities/TextObjectFormatter.h"


namespace BitFunnel
{

    struct ExpectedAndInputs
    {
    public:
        char const * m_expected;
        char const * m_input;
    };

    const ExpectedAndInputs c_testData[] =
    {
        // UNIGRAM.

        // This case came from an actual bug involving utf-8 characters passed
        // to isspace. In this case a value of -61 was passed to isspace(),
        // triggering an exception. isspace() was subsequently replaced with
        // BitFunnel::IsSpace().
        { "Unigram(\"françois\", 0)", "françois" },
        { "Unigram(\"wat\", 0)", "wat" },

        // STREAM:UNIGRAM.
        {"Unigram(\"wat\", 1)", "stream:wat"},

        // (UNIGRAM)
        {"Unigram(\"wat\", 0)", "(wat)"},

        // OR of two UNIGRAMs.
        {
            "Or {\n"
            "  Children: [\n"
            "    Unigram(\"foo\", 0),\n"
            "    Unigram(\"wat\", 0)\n"
            "  ]\n"
            "}",
            "wat|foo"
         },

        // OR of two UNIGRAMs with parens.
        {
            "Or {\n"
            "  Children: [\n"
            "    Unigram(\"foo\", 0),\n"
            "    Unigram(\"wat\", 0)\n"
            "  ]\n"
            "}",
            "(wat|foo)"
        },

        // OR of two UNIGRAMs with parens.
        {
            "Or {\n"
            "  Children: [\n"
            "    Unigram(\"foo\", 0),\n"
            "    Unigram(\"wat\", 0)\n"
            "  ]\n"
            "}",
            " (wat|foo)\t"
        },

        // OR of two UNIGRAMs with parens.
        {
            "Or {\n"
            "  Children: [\n"
            "    Unigram(\"foo\", 0),\n"
            "    Unigram(\"wat\", 0)\n"
            "  ]\n"
            "}",
            "\t( wat |\tfoo )"
        },

        // NOT.
        {
            "Not {\n"
            "  Child: Unigram(\"wat\", 0)\n"
            "}"
            , "-wat"
        },

        // AND of 2.
        {
            "And {\n"
            "  Children: [\n"
            "    Unigram(\"foo\", 0),\n"
            "    Unigram(\"wat\", 0)\n"
            "  ]\n"
            "}",
            "wat foo"
        },

        // AND of 2, explicit '&'.
        {
            "And {\n"
            "  Children: [\n"
            "    Unigram(\"foo\", 0),\n"
            "    Unigram(\"wat\", 0)\n"
            "  ]\n"
            "}",
            "wat&foo"
        },

        // AND of 2, explicit '&' with whitespace.
        {
            "And {\n"
            "  Children: [\n"
            "    Unigram(\"foo\", 0),\n"
            "    Unigram(\"wat\", 0)\n"
            "  ]\n"
            "}",
            "wat\t\t&  foo"
        },

        // PHRASE.
        {
            "Phrase {\n"
            "  StreamId: 0,\n"
            "  Grams: [\n"
            "    \"wat\",\n"
            "    \"foo\"\n"
            "  ]\n"
            "}",
            "\" wat\tfoo\""
        },

        // PHRASE.
        {
            "Phrase {\n"
            "  StreamId: 0,\n"
            "  Grams: [\n"
            "    \"wat\",\n"
            "    \"foo\"\n"
            "  ]\n"
            "}",
            "\"wat\tfoo\""
        },

        // OR of AND.
        {
            "Or {\n"
            "  Children: [\n"
            "    Unigram(\"three\", 0),\n"
            "    And {\n"
            "      Children: [\n"
            "        Unigram(\"two\", 0),\n"
            "        Unigram(\"one\", 0)\n"
            "      ]\n"
            "    }\n"
            "  ]\n"
            "}",
            "one two | three"
        },

        // OR of AND.
        {
            "Or {\n"
            "  Children: [\n"
            "    And {\n"
            "      Children: [\n"
            "        Unigram(\"four\", 0),\n"
            "        Unigram(\"three\", 0)\n"
            "      ]\n"
            "    },\n"
            "    And {\n"
            "      Children: [\n"
            "        Unigram(\"two\", 0),\n"
            "        Unigram(\"one\", 0)\n"
            "      ]\n"
            "    }\n"
            "  ]\n"
            "}",
            "one\ttwo|three    \tfour"
        },

        // AND with NOT.
        {
            "And {\n"
            "  Children: [\n"
            "    Not {\n"
            "      Child: Unigram(\"two\", 0)\n"
            "    },\n"
            "    Unigram(\"one\", 0)\n"
            "  ]\n"
            "}",
            "one&-two"
        },

        // AND with NOT.
        {
            "And {\n"
            "  Children: [\n"
            "    Not {\n"
            "      Child: Unigram(\"two\", 0)\n"
            "    },\n"
            "    Unigram(\"one\", 0)\n"
            "  ]\n"
            "}",
            "one -two"
        },

        // AND with NOT.
        // TODO: this is probably counter-intuitive to users.
        {
            "And {\n"
            "  Children: [\n"
            "    Not {\n"
            "      Child: Unigram(\"two\", 0)\n"
            "    },\n"
            "    Unigram(\"one\", 0)\n"
            "  ]\n"
            "}",
            "one-two"
        },

        // AND with NOT.
        {
            "And {\n"
            "  Children: [\n"
            "    Not {\n"
            "      Child: Unigram(\"two\", 0)\n"
            "    },\n"
            "    Unigram(\"one\", 0)\n"
            "  ]\n"
            "}",
            "one- two"
        },

        // AND with parens.
        {
            "And {\n"
            "  Children: [\n"
            "    Unigram(\"two\", 0),\n"
            "    Unigram(\"one\", 0)\n"
            "  ]\n"
            "}",
            "one (two)"
        },

        // OR with NOT.
        {
            "Or {\n"
            "  Children: [\n"
            "    Not {\n"
            "      Child: Unigram(\"two\", 0)\n"
            "    },\n"
            "    Unigram(\"one\", 0)\n"
            "  ]\n"
            "}",
            "one|-two"
        },

        // OR with NOT.
        {
            "Or {\n"
            "  Children: [\n"
            "    Not {\n"
            "      Child: Unigram(\"two\", 0)\n"
            "    },\n"
            "    Unigram(\"one\", 0)\n"
            "  ]\n"
            "}",
            " one    | -    two "
        },

        // AND then OR.
        {
            "Or {\n"
            "  Children: [\n"
            "    Unigram(\"three\", 0),\n"
            "    And {\n"
            "      Children: [\n"
            "        Unigram(\"two\", 0),\n"
            "        Unigram(\"one\", 0)\n"
            "      ]\n"
            "    }\n"
            "  ]\n"
            "}",
            "one & two | three"
        },

        // AND then OR, parens change precedence.
        {
            "And {\n"
            "  Children: [\n"
            "    Or {\n"
            "      Children: [\n"
            "        Unigram(\"three\", 0),\n"
            "        Unigram(\"two\", 0)\n"
            "      ]\n"
            "    },\n"
            "    Unigram(\"one\", 0)\n"
            "  ]\n"
            "}",
            "one & (two | three)"
        },

        // AND of PHRASE
        {
            "And {\n"
            "  Children: [\n"
            "    Phrase {\n"
            "      StreamId: 0,\n"
            "      Grams: [\n"
            "        \"three\",\n"
            "        \"four\"\n"
            "      ]\n"
            "    },\n"
            "    Phrase {\n"
            "      StreamId: 0,\n"
            "      Grams: [\n"
            "        \"one\",\n"
            "        \"two\"\n"
            "      ]\n"
            "    }\n"
            "  ]\n"
            "}",
            "\"one two\" \"three four\""
        },

        // AND of PHRASE
        {
            "And {\n"
            "  Children: [\n"
            "    Phrase {\n"
            "      StreamId: 0,\n"
            "      Grams: [\n"
            "        \"three\",\n"
            "        \"four\"\n"
            "      ]\n"
            "    },\n"
            "    Phrase {\n"
            "      StreamId: 0,\n"
            "      Grams: [\n"
            "        \"one\",\n"
            "        \"two\"\n"
            "      ]\n"
            "    }\n"
            "  ]\n"
            "}",
            "\"one two\"&\"three four\""
        },

        // OR of PHRASE
        {
            "Or {\n"
            "  Children: [\n"
            "    Phrase {\n"
            "      StreamId: 0,\n"
            "      Grams: [\n"
            "        \"three\",\n"
            "        \"four\"\n"
            "      ]\n"
            "    },\n"
            "    Phrase {\n"
            "      StreamId: 0,\n"
            "      Grams: [\n"
            "        \"one\",\n"
            "        \"two\"\n"
            "      ]\n"
            "    }\n"
            "  ]\n"
            "}",
            "\"one two\"|\"three four\""
        },

        // ESCAPE.
        {
            "Unigram(\"one|two\", 0)",
            "one\\|two"
        },

        // PHRASE with ESCAPE.
        {
            "Phrase {\n"
            "  StreamId: 0,\n"
            "  Grams: [\n"
            "    \"one|two\",\n"
            "    \"three\"\n"
            "  ]\n"
            "}",
            "\"one\\|two three\""
        },

        // PHRASE with quote ESCAPE.
        {
            "Phrase {\n"
            "  StreamId: 0,\n"
            "  Grams: [\n"
            "    \"one\\\"two\",\n"
            "    \"three\"\n"
            "  ]\n"
            "}",
            "\"one\\\"two three\""
        },

        // NOT of OR.
        {
            "Not {\n"
            "  Child: Or {\n"
            "    Children: [\n"
            "      Unigram(\"two\", 0),\n"
            "      Unigram(\"one\", 0)\n"
            "    ]\n"
            "  }\n"
            "}",
            "-(one|two)"
        }
    };

    void VerifyQueryParser(std::string const & expected, std::string const & input, IAllocator& allocator)
    {
        allocator.Reset();

        auto streamConfiguration = Factories::CreateStreamConfiguration();
        streamConfiguration->AddMapping("body", { 123 });
        streamConfiguration->AddMapping("stream", { 123 });
        QueryParser parser(input.c_str(), *streamConfiguration, allocator);

        //std::cout
        //    << "============================" << std::endl
        //    << "input = \"" << input << "\"" << std::endl;

        auto result = parser.Parse();
        ASSERT_NE(nullptr, result);

        std::stringstream parsedOutput;
        TextObjectFormatter formatter(parsedOutput);
        result->Format(formatter);

        //std::cout << "output: \"" << parsedOutput.str() << "\"" << std::endl;
        EXPECT_EQ(expected, parsedOutput.str());
    }


    TEST(QueryParser, Trivial)
    {
        Allocator allocator(4096);

        for (size_t i = 0; i < sizeof(c_testData) / sizeof(ExpectedAndInputs); ++i)
        {
            VerifyQueryParser(c_testData[i].m_expected, c_testData[i].m_input, allocator);
        }
    }


    TEST(QueryParser, Escaping)
    {
        char const * input = "A B\tC\fD\vE&F|G\\H(I)J\"K:L-M";
        char const * expected = "A\\ B\\\tC\\\fD\\\vE\\&F\\|G\\\\H\\(I\\)J\\\"K\\:L\\-M";

        auto observed = QueryParser::Escape(input);

        EXPECT_EQ(expected, observed);
    }
}
