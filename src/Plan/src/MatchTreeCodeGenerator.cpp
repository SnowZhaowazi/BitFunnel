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
// THE SOFTWARE

#include <iostream>

#include "BitFunnel/Index/DocumentHandle.h"
#include "CompileNode.h"
#include "NativeJIT/CodeGen/ExecutionBuffer.h"
#include "NativeJIT/CodeGen/FunctionBuffer.h"
#include "NativeJIT/CodeGenHelpers.h"
#include "NativeJIT/Function.h"
#include "NativeJIT/CodeGen/Register.h"
#include "MachineCodeGenerator.h"
#include "MatchTreeCodeGenerator.h"
#include "Temporary/Allocator.h"

#include "MatchTreeCodeGenerator.h"
#include "RegisterAllocator.h"


namespace BitFunnel
{
    void CompilePseudoCode(char * /*RCX*/, char* /*RDX*/, ptrdiff_t const * /*RSI*/)
    {
    }

    void PseudoCode(MatcherNode::Parameters& params)
    {
        auto RDI = params;
        auto RSI = RDI.m_rowOffsets;

        while (RDI.m_sliceCount > 0)
        {
            auto RCX = *RDI.m_sliceBuffers;
            auto RDX = RCX;

            auto limit = RCX + (RDI.m_iterationsPerSlice);
            while (RCX < limit)
            {
                CompilePseudoCode(RCX, RDX, RSI);

                ++RCX;
            }

            --params.m_sliceCount;
            ++params.m_sliceBuffers;
        }
    }


    //*************************************************************************
    //
    // MatcherNode
    //
    //*************************************************************************
    MatcherNode::MatcherNode(Prototype& expression,
                             CompileNode const & compileNodeTree,
                             RegisterAllocator const & registers)
      : Node(expression),
        m_compileNodeTree(compileNodeTree),
        m_registers(registers)
    {
    }


    ExpressionTree::Storage<size_t> MatcherNode::CodeGenValue(ExpressionTree& tree)
    {
        EmitRegisterInitialization(tree);
        EmitOuterLoop(tree);

        auto result = tree.Direct<size_t>();
        return result;
    }


    void MatcherNode::EmitRegisterInitialization(ExpressionTree& tree)
    {
        auto & code = tree.GetCodeGenerator();

        // Abstract away the ABI differences here.
#if BITFUNNEL_PLATFORM_WINDOWS
        m_param1 = rcx;
        m_return = rax;

        // rcx holds first parameter - transfer to rdi.
        code.Emit<OpCode::Mov>(rdi, m_param1);
#else
        m_param1 = rdi;
        m_return = rax;

        // rdi already holds first parameter. No need to load.
#endif

        // Allocate temporary variables.
        m_innerLoopLimit = tree.Temporary<size_t>();

        // Initialize row pointers.
        // RSI has pointer to row offsets.
        code.Emit<OpCode::Mov>(rsi, rdi, m_rowOffsets);

        // Load row offsets into R8..R8 + m_registers.GetRegistersAllocated()
        // TODO: Should we use GetRegister() or explicitly use r+8?
        // The former offers an opportunity to misconfigure the register allocator
        // to use registers that conflict with other matcher registers.
        for (unsigned r = 0; r < m_registers.GetRegistersAllocated(); ++r)
        {
            code.Emit<OpCode::Mov>(Register<8u, false>(r + 8),
                                   rsi,
                                   m_registers.GetRowIdFromRegister(r) * 8);
            // TODO: Would the following alternative be better?
            // This code may not work with the existing CompileNode compiler.
            //code.Emit<OpCode::Mov>(Register<8u, false>(m_registers.GetRegister(r)),
            //                       rsi,
            //                       m_registers.GetRowIdFromRegister(r) * 8);
        }
    }


    void MatcherNode::EmitOuterLoop(ExpressionTree& tree)
    {
        auto & code = tree.GetCodeGenerator();

        auto topOfLoop = code.AllocateLabel();
        auto bottomOfLoop = code.AllocateLabel();


        //
        // Top of loop
        //
        code.PlaceLabel(topOfLoop);

        // Check if the slice count (loop counter) reaches zero.
        code.Emit<OpCode::Mov>(rax, rdi, m_sliceCount);
        code.Emit<OpCode::Or>(rax, rax);
        code.EmitConditionalJump<JccType::JZ>(bottomOfLoop);

        EmitInnerLoop(tree);

        // Decrement the slice count by 1.
        code.Emit<OpCode::Dec, 8>(rdi, m_sliceCount);

        // Advance to the next slice.
        code.EmitImmediate<OpCode::Mov>(rax, 8);
        code.Emit<OpCode::Add>(rdi, m_sliceBuffers, rax);

        code.Jmp(topOfLoop);

        //
        // Bottom of loop
        //
        code.PlaceLabel(bottomOfLoop);
    }


    void MatcherNode::EmitInnerLoop(ExpressionTree& tree)
    {
        auto & code = tree.GetCodeGenerator();

        auto topOfLoop = code.AllocateLabel();
        auto bottomOfLoop = code.AllocateLabel();
        auto exitLoop = code.AllocateLabel();

        // Initialize loop counter and limit.
        //   rcx: loop counter starts at the current slice buffer pointer.
        //   m_innerLoopLimit: slice buffer pointer + bytes in starting row.
        code.Emit<OpCode::Mov>(rdx, rdi, m_sliceBuffers);
        code.Emit<OpCode::Mov>(rdx, rdx, 0);
        code.Emit<OpCode::Mov>(rax, rdi, m_iterationsPerSlice);
        code.EmitImmediate<OpCode::Shl>(rax, static_cast<uint8_t>(3));
        code.Emit<OpCode::Add>(rax, rdx);
        CodeGenHelpers::Emit<OpCode::Mov>(code, m_innerLoopLimit, rax);
        code.Emit<OpCode::Mov>(rcx, rdx);


        //
        // Top of loop
        //
        code.PlaceLabel(topOfLoop);

        // Exit when loop counter rcx == m_innerLoopLimit
        CodeGenHelpers::Emit<OpCode::Cmp>(code, rcx, m_innerLoopLimit);
        code.EmitConditionalJump<JccType::JE>(exitLoop);    // TODO: Original code passed X64::Long.

        //
        // Body of loop
        //

        // TODO: Handle case where there are no rows.

        {
            MachineCodeGenerator generator(m_registers, tree.GetCodeGenerator());
            m_compileNodeTree.Compile(generator);
        }

        EmitFinishIteration(tree);

        //
        // Bottom of loop
        //
        code.PlaceLabel(bottomOfLoop);
        code.EmitImmediate<OpCode::Add>(rcx, 8);    // Increment current offset.
        code.Jmp(topOfLoop);


        code.PlaceLabel(exitLoop);
    }


    void MatcherNode::EmitFinishIteration(ExpressionTree& tree)
    {
        auto & code = tree.GetCodeGenerator();

        // Check whether there are any matches.
        auto noMatches = code.AllocateLabel();
        code.Emit<OpCode::Mov>(rax, rdi, m_dedupe);
        code.Emit<OpCode::Or>(rax, rax);
        code.EmitConditionalJump<JccType::JZ>(noMatches);

        // Save registers.
        code.Emit<OpCode::Push>(r9);
        code.Emit<OpCode::Push>(r10);
        code.Emit<OpCode::Push>(r11);
        code.Emit<OpCode::Push>(r12);
        code.Emit<OpCode::Push>(r13);
        code.Emit<OpCode::Push>(r14);
        code.Emit<OpCode::Push>(r15);

        // Initialize loop invariants.
        // r10 has m_matches.
        code.Emit<OpCode::Mov>(r10, rdi, m_matches);

        // r9 has the Slice* extracted from the slice buffer pointer in rdx.
        code.Emit<OpCode::Mov>(r9, rdx, 0);

        auto quadwordLoopTop = code.AllocateLabel();
        auto quadwordLoopExit = code.AllocateLabel();

        // Each bit in rax corresponds to a quadword with a match.
        code.Emit<OpCode::Mov>(rax, rdi, m_dedupe);

        //
        // Top of quadword loop.
        //

        code.PlaceLabel(quadwordLoopTop);
        code.Emit<OpCode::Bsf>(r15, rax);
        code.EmitConditionalJump<JccType::JZ>(quadwordLoopExit);

        //
        // Body of quadword loop.
        //

        auto bitLoopTop = code.AllocateLabel();
        auto bitLoopExit = code.AllocateLabel();

        code.Emit<OpCode::Mov>(r14, rdi, r15, SIB::Scale8, 8 + m_dedupe);

        //
        // Top of bit loop.
        //

        code.PlaceLabel(bitLoopTop);
        code.Emit<OpCode::Bsf>(r13, r14);
        code.EmitConditionalJump<JccType::JZ>(bitLoopExit);

        EmitStoreMatch(tree);

        //
        // Bottom of bit loop.
        //

        code.Emit<OpCode::Btr>(r14, r13);
        code.Jmp(bitLoopTop);


        code.PlaceLabel(bitLoopExit);
        code.Emit<OpCode::Mov>(rdi, r15, SIB::Scale8, 8 + m_dedupe, r14);


        //
        // Bottom of quadword loop.
        //

        code.Emit<OpCode::Btr>(rax, r15);
        code.Jmp(quadwordLoopTop);


        //
        // Exit quadword loop.
        //

        code.PlaceLabel(quadwordLoopExit);

        // Write zero'd out rax to m_dedupe in preparation
        // for next matcher iteration.
        code.Emit<OpCode::Mov>(rdi, m_dedupe, rax);

        // Restore registers.
        code.Emit<OpCode::Pop>(r15);
        code.Emit<OpCode::Pop>(r14);
        code.Emit<OpCode::Pop>(r13);
        code.Emit<OpCode::Pop>(r12);
        code.Emit<OpCode::Pop>(r11);
        code.Emit<OpCode::Pop>(r10);
        code.Emit<OpCode::Pop>(r9);

        code.PlaceLabel(noMatches);
    }


    // If there is space, stores (Slice*, DocIndex) for match in
    //   m_matches[m_matchCount++]
    // Clobbers r10, r11, r12.
    // Assumes
    //   rdx has slice buffer pointer.
    //   r13 has bit position of match.
    //   r15 has quadword number of match.
    //   r10 has m_matches
    //   r9 has the Slice*
    void MatcherNode::EmitStoreMatch(ExpressionTree & tree)
    {
        auto & code = tree.GetCodeGenerator();

        // Save match here.
        //   Bit position is in r13.
        //   Quadword number is in r15.
        auto outOfSpace = code.AllocateLabel();

        // Load index of next match into r12.
        // See if there is space for another match.
        code.Emit<OpCode::Mov>(r12, rdi, m_matchCount);
        code.Emit<OpCode::Cmp>(r12, rdi, m_capacity);
        code.EmitConditionalJump<JccType::JZ>(outOfSpace);

        // Convert index to byte offset. Each DocHandle record is 16 bytes.
        code.EmitImmediate<OpCode::Shl>(r12, static_cast<uint8_t>(4));

        // Compute DocIndex in r11.
        code.Emit<OpCode::Mov>(r11, r15);
        code.EmitImmediate<OpCode::Shl>(r11, static_cast<uint8_t>(3));
        code.Emit<OpCode::Add>(r11, r13);


        // Store Slice* at offset 0 of the DocHandle.
        code.Emit<OpCode::Mov>(r10, r12, SIB::Scale1, 0, r9);

        // Store index at offset 8 of the DocHandle.
        code.Emit<OpCode::Mov>(r10, r12, SIB::Scale1, 8, r11);

        // m_matchCount++
        code.Emit<OpCode::Inc, 8>(rdi, m_matchCount);

        code.PlaceLabel(outOfSpace);
    }


    void MatcherNode::Print(std::ostream& out) const
    {
        this->PrintCoreProperties(out, "MatcherNode");

        //        out << ", scorePlan = " << m_left.GetId();
    }


    //*************************************************************************
    //
    // MatchTreeCompiler
    //
    //*************************************************************************
    MatchTreeCompiler::MatchTreeCompiler(ExecutionBuffer & codeAllocator,
                                         Allocator & treeAllocator,
                                         CompileNode const & tree,
                                         RegisterAllocator const & registers)
        : m_code(codeAllocator, 8192)
    {
        MatcherNode::Prototype expression(treeAllocator, m_code);
        expression.EnableDiagnostics(std::cout);

        auto & node = expression.PlacementConstruct<MatcherNode>(expression,
                                                                 tree,
                                                                 registers);
        m_function = expression.Compile(node);
    }


    size_t MatchTreeCompiler::Run(size_t sliceCount,
                                  char * const * sliceBuffers,
                                  size_t iterationsPerSlice,
                                  ptrdiff_t const * rowOffsets)
    {
        const size_t matchCount = 100;

        std::vector<MatcherNode::Record> matches(matchCount, { nullptr, 0 });


        MatcherNode::Parameters parameters = {
            sliceCount,
            sliceBuffers,
            iterationsPerSlice,
            rowOffsets,
            &CallbackHelper,
            {0},
            matchCount,
            0,
            matches.data()
        };

        auto result = m_function(&parameters);

        std::cout
            << parameters.m_matchCount
            << " matches."
            << std::endl;

        for (size_t i = 0; i < parameters.m_matchCount; ++i)
        {
            auto & match = parameters.m_matches[i];
            std::cout
                << std::hex
                << match.m_buffer
                << std::dec
                << ": "
                << match.m_id
                << std::endl;
        }

        return result;
    }


    size_t MatchTreeCompiler::CallbackHelper(//MatchTreeCompiler& /*node*/,
                                             size_t value)
    {
        std::cout
            << "CallbackHelper("
            << value
            << ")"
            << std::endl;

        return 1234567ull;
    }
}
