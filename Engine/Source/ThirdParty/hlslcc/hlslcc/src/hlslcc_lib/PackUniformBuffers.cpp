// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "ShaderCompilerCommon.h"
#include "glsl_parser_extras.h"
#include "ir.h"
#include "ir_visitor.h"
#include "ir_rvalue_visitor.h"
#include "PackUniformBuffers.h"
#include "IRDump.h"
#include "ast.h"
//@todo-rco: Remove STL!
#include <algorithm>
#include <sstream>

#include <vector>
typedef TArray<ir_variable*> TIRVarVector;


template <typename T>
static inline T MIN2(T a, T b)
{
	return a < b ? a : b;
}

template <typename T>
static inline T MAX2(T a, T b)
{
	return a > b ? a : b;
}

static std::string GetUniformArrayName(_mesa_glsl_parser_targets Target, glsl_base_type Type, int CBIndex)
{
	std::stringstream Name("");

	Name << glsl_variable_tag_from_parser_target(Target);

	if (CBIndex == -1)
	{
		Name << "u_";
	}
	else
	{
		Name << "c" << CBIndex << "_";
	}

	Name << (char)GetArrayCharFromPrecisionType(Type, false);
	Name.flush();
	return Name.str();
}

struct SFixSimpleArrayDereferencesVisitor : ir_rvalue_visitor
{
	_mesa_glsl_parse_state* ParseState;
	exec_list* FunctionBody;
	TVarVarMap& UniformMap;

	SFixSimpleArrayDereferencesVisitor(_mesa_glsl_parse_state* InParseState, exec_list* InFunctionBody, TVarVarMap& InUniformMap) :
		ParseState(InParseState),
		FunctionBody(InFunctionBody),
		UniformMap(InUniformMap)
	{
	}

	virtual void handle_rvalue(ir_rvalue** RValuePointer) override
	{
		static int TempID = 0;

		if (RValuePointer && *RValuePointer)
		{
			ir_rvalue* RValue = *RValuePointer;
			if (RValue && RValue->as_dereference_array())
			{
				ir_dereference_array* DerefArray = RValue->as_dereference_array();
				ir_variable* ArrayVar = RValue->variable_referenced();
				const glsl_type* ArrayElementType = ArrayVar->type->element_type();
				if (ArrayVar->read_only && ArrayElementType && !ArrayElementType->is_matrix())
				{
					if (ArrayVar->mode == ir_var_auto)
					{
						TVarVarMap::iterator itFound = UniformMap.find(ArrayVar);
						if (itFound != UniformMap.end())
						{
							ir_variable* NewLocal = new(ParseState) ir_variable(ArrayElementType, ralloc_asprintf(ParseState, "ar%d", TempID++), ir_var_auto);
							*RValuePointer = new(ParseState) ir_dereference_variable(NewLocal);

							SUniformVarEntry& Entry = itFound->second;

							ir_constant* ArrayBaseOffset = (DerefArray->array_index->type->base_type == GLSL_TYPE_UINT) ?
								new(ParseState) ir_constant((unsigned)Entry.Vec4Start) :
								new(ParseState) ir_constant(Entry.Vec4Start);
							ir_expression* NewArrayIndex = new(ParseState) ir_expression(ir_binop_add, ArrayBaseOffset, DerefArray->array_index);
							ir_dereference_array* NewDerefArray = new(ParseState) ir_dereference_array(new(ParseState) ir_dereference_variable(Entry.UniformArrayVar), NewArrayIndex);

							ir_swizzle* NewSwizzle = new(ParseState) ir_swizzle(
								NewDerefArray,
								MIN2(Entry.Components + 0, 3),
								MIN2(Entry.Components + 1, 3),
								MIN2(Entry.Components + 2, 3),
								MIN2(Entry.Components + 3, 3),
								ArrayElementType->vector_elements
								);

							ir_assignment* NewLocalInitializer = new(ParseState) ir_assignment(new(ParseState) ir_dereference_variable(NewLocal), NewSwizzle);
							base_ir->insert_before(NewLocalInitializer);
							NewLocalInitializer->insert_before(NewLocal);
						}
					}
				}
				else if (ArrayVar->read_only && ArrayElementType && ArrayElementType->is_matrix())
				{
					//matrix path
					if (ArrayVar->mode == ir_var_auto)
					{
						TVarVarMap::iterator itFound = UniformMap.find(ArrayVar);
						if (itFound != UniformMap.end())
						{
							ir_variable* NewLocal = new(ParseState) ir_variable(ArrayElementType, ralloc_asprintf(ParseState, "ar%d", TempID++), ir_var_auto);
							*RValuePointer = new(ParseState) ir_dereference_variable(NewLocal);

							SUniformVarEntry& Entry = itFound->second;

							exec_list instructions;
							instructions.push_tail(NewLocal);

							// matrix construction goes column by column performing an assignment
							for (int i = 0; i < ArrayElementType->matrix_columns; i++)
							{
								// Offset baking in matrix column
								ir_constant* ArrayBaseOffset = (DerefArray->array_index->type->base_type == GLSL_TYPE_UINT) ?
									new(ParseState) ir_constant((unsigned)(Entry.Vec4Start + i)) :
									new(ParseState) ir_constant((int)(Entry.Vec4Start + i));
								// Scale index by matrix columns
								ir_constant* ArrayScale = (DerefArray->array_index->type->base_type == GLSL_TYPE_UINT) ?
									new(ParseState) ir_constant((unsigned)(ArrayElementType->matrix_columns)) :
									new(ParseState) ir_constant((int)(ArrayElementType->matrix_columns));
								ir_rvalue* BaseIndex = DerefArray->array_index->clone(ParseState, NULL);
								ir_expression* NewArrayScale = new(ParseState) ir_expression(ir_binop_mul, BaseIndex, ArrayScale);
								// Compute final matrix address
								ir_expression* NewArrayIndex = new(ParseState) ir_expression(ir_binop_add, ArrayBaseOffset, NewArrayScale);
								ir_dereference_array* NewDerefArray = new(ParseState) ir_dereference_array(new(ParseState) ir_dereference_variable(Entry.UniformArrayVar), NewArrayIndex);

								ir_swizzle* NewSwizzle = new(ParseState) ir_swizzle(
									NewDerefArray,
									MIN2(Entry.Components + 0, 3),
									MIN2(Entry.Components + 1, 3),
									MIN2(Entry.Components + 2, 3),
									MIN2(Entry.Components + 3, 3),
									ArrayElementType->vector_elements
									);

								ir_assignment* NewLocalInitializer = new(ParseState) ir_assignment(new(ParseState) ir_dereference_array(NewLocal, new(ParseState) ir_constant(i)), NewSwizzle);
								instructions.push_tail(NewLocalInitializer);
							}
							base_ir->insert_before(&instructions);
						}
					}
				}
			}
		}
	}
};


struct SFindStructMembersVisitor : public ir_rvalue_visitor
{
	TIRVarSet& FoundRecordVars;

	SFindStructMembersVisitor(TIRVarSet& InFoundRecordVars) :
		FoundRecordVars(InFoundRecordVars)
	{
	}

	virtual void handle_rvalue(ir_rvalue** RValuePointer) override
	{
		if (RValuePointer && *RValuePointer)
		{
			ir_rvalue* RValue = *RValuePointer;
			if (RValue && RValue->as_dereference_record())
			{
				ir_variable* RecordVar = RValue->variable_referenced();
				if (RecordVar->mode == ir_var_uniform)
				{
					check(RecordVar->type->is_record());
					check(RecordVar->semantic && *RecordVar->semantic);
					FoundRecordVars.insert(RecordVar);
				}
			}
		}
	}
};



struct SConvertStructMemberToUniform : ir_rvalue_visitor
{
	_mesa_glsl_parse_state* ParseState;
	TStringStringIRVarMap& UniformMap;

	SConvertStructMemberToUniform(_mesa_glsl_parse_state* InParseState, TStringStringIRVarMap& InUniformMap) :
		ParseState(InParseState),
		UniformMap(InUniformMap)
	{
	}

	virtual void handle_rvalue(ir_rvalue** RValuePointer) override
	{
		if (RValuePointer && *RValuePointer)
		{
			ir_rvalue* RValue = *RValuePointer;
			if (RValue && RValue->as_dereference_record())
			{
				ir_dereference_record* DerefStruct = RValue->as_dereference_record();
				ir_variable* StructVar = RValue->variable_referenced();
				check(StructVar);
				if (StructVar->name)
				{
					// Name can be NULL when working on inputs to geometry shader structures
					TStringStringIRVarMap::iterator FoundStructIter = UniformMap.find(StructVar->name);
					if (FoundStructIter != UniformMap.end())
					{
						TStringIRVarMap::iterator FoundMember = FoundStructIter->second.find(DerefStruct->field);
						check(FoundMember != FoundStructIter->second.end());
						*RValuePointer = new(ParseState) ir_dereference_variable(FoundMember->second);
					}
				}
			}
		}
	}
};


// Flattens structures inside a uniform buffer into uniform variables; from:
//		cbuffer CB
//		{
//			float4 Value0;
//			struct
//			{
//				float4 Member0;
//				float3 Member1;
//			} S;
//			float4 Value1;
//		};
//	to:
//		cbuffer CB
//		{
//			float4 Value;
//			float4 S_Member0;
//			float3 S_Member1;
//			float4 Value1;
//		};
void FlattenUniformBufferStructures(exec_list* Instructions, _mesa_glsl_parse_state* ParseState)
{
	//IRDump(Instructions, ParseState, "Before FlattenUniformBufferStructures()");

	// Populate
	TIRVarSet StructVars;
	foreach_iter(exec_list_iterator, Iter, *Instructions)
	{
		ir_instruction* Instruction = (ir_instruction*)Iter.get();
		ir_function* Function = Instruction->as_function();
		if (Function)
		{
			foreach_iter(exec_list_iterator, SigIter, *Function)
			{
				ir_function_signature* Sig = (ir_function_signature *)SigIter.get();
				if (!Sig->is_builtin && Sig->is_defined)
				{
					SFindStructMembersVisitor FindMembersVisitor(StructVars);
					FindMembersVisitor.run(&Sig->body);
				}
			}
		}
		else if (Instruction->ir_type == ir_type_variable)
		{
			ir_variable* Var = (ir_variable*)Instruction;
			if (Var->mode == ir_var_uniform && Var->type->is_record())
			{
				check(Var->semantic && *Var->semantic);
				StructVars.insert(Var);
			}
		}
	}

	if (StructVars.empty())
	{
		// Nothing to do if no structs found; just copy the original state
		ParseState->CBuffersStructuresFlattened = ParseState->CBuffersOriginal;
		return;
	}

	// Find all CBs that need to be flattened
	unsigned UsedCBsMask = 0;
	for (TIRVarSet::iterator VarIter = StructVars.begin(); VarIter != StructVars.end(); ++VarIter)
	{
		ir_variable* var = *VarIter;
		for (unsigned i = 0; i < ParseState->num_uniform_blocks; ++i)
		{
			if (!strcmp(ParseState->uniform_blocks[i]->name, var->semantic))
			{
				UsedCBsMask |= 1 << i;
				break;
			}
		}
	}

	// Add the unchanged ones first
	for (unsigned i = 0; i < ParseState->num_uniform_blocks; ++i)
	{
		if ((UsedCBsMask & (1 << i)) == 0)
		{
			SCBuffer* CBuffer = ParseState->FindCBufferByName(false, ParseState->uniform_blocks[i]->name);
			check(CBuffer);
			ParseState->CBuffersStructuresFlattened.push_back(*CBuffer);
		}
	}

	// Now Flatten and store member info
	TStringStringIRVarMap StructMemberMap;
	for (TIRVarSet::iterator VarIter = StructVars.begin(); VarIter != StructVars.end(); ++VarIter)
	{
		ir_variable* var = *VarIter;

		// Find UB index
		int UniformBufferIndex = -1;
		for (unsigned i = 0; i < ParseState->num_uniform_blocks; ++i)
		{
			if (!strcmp(ParseState->uniform_blocks[i]->name, var->semantic))
			{
				UniformBufferIndex = (int)i;
				break;
			}
		}
		check(UniformBufferIndex != -1);

		bool bNeedToAddUB = (UsedCBsMask & (1 << UniformBufferIndex)) != 0;
		const glsl_uniform_block* OriginalUB = ParseState->uniform_blocks[UniformBufferIndex];

		// Copy the cbuffer list with room for the expanded values
		glsl_uniform_block* NewUniformBlock = NULL;

		if (bNeedToAddUB)
		{
			NewUniformBlock = glsl_uniform_block::alloc(ParseState, OriginalUB->num_vars - 1 + var->type->length);
			NewUniformBlock->name = OriginalUB->name;
		}
		else
		{
			UsedCBsMask |= 1 << UniformBufferIndex;
		}

		SCBuffer CBuffer;
		CBuffer.Name = OriginalUB->name;

		// Now find this struct member in the cbuffer and flatten it
		ir_variable* UniformBufferMemberVar = NULL;
		unsigned NewMemberIndex = 0;
		for (unsigned MemberIndex = 0; MemberIndex < OriginalUB->num_vars; ++MemberIndex)
		{
			if (!strcmp(OriginalUB->vars[MemberIndex]->name, var->name))
			{
				check(!UniformBufferMemberVar);
				UniformBufferMemberVar = OriginalUB->vars[MemberIndex];

				// Go through each member and add a new entry on the uniform buffer
				for (unsigned StructMemberIndex = 0; StructMemberIndex < var->type->length; ++StructMemberIndex)
				{
					ir_variable* NewLocal = new (ParseState) ir_variable(var->type->fields.structure[StructMemberIndex].type, ralloc_asprintf(ParseState, "%s_%s", var->name, var->type->fields.structure[StructMemberIndex].name), ir_var_uniform);
					NewLocal->semantic = var->semantic; // alias semantic to specify the uniform block.
					NewLocal->read_only = true;

					StructMemberMap[var->name][var->type->fields.structure[StructMemberIndex].name] = NewLocal;
					if (bNeedToAddUB)
					{
						NewUniformBlock->vars[NewMemberIndex++] = NewLocal;
						CBuffer.AddMember(NewLocal->type, NewLocal);
					}

					Instructions->push_head(NewLocal);
				}
			}
			else
			{
				if (bNeedToAddUB)
				{
					NewUniformBlock->vars[NewMemberIndex++] = OriginalUB->vars[MemberIndex];
					CBuffer.AddMember(OriginalUB->vars[MemberIndex]->type, OriginalUB->vars[MemberIndex]);
				}
			}
		}

		if (bNeedToAddUB)
		{
			check(NewMemberIndex == NewUniformBlock->num_vars);

			// Replace the original UB with this new one
			ParseState->uniform_blocks[UniformBufferIndex] = NewUniformBlock;
			ParseState->CBuffersStructuresFlattened.push_back(CBuffer);
		}

		// Downgrade the structure variable to a local
		var->mode = ir_var_temporary;
	}

	// Finally replace the struct member accesses into regular member access
	foreach_iter(exec_list_iterator, Iter, *Instructions)
	{
		ir_instruction* Instruction = (ir_instruction*)Iter.get();
		ir_function* Function = Instruction->as_function();
		if (Function)
		{
			foreach_iter(exec_list_iterator, SigIter, *Function)
			{
				ir_function_signature* Sig = (ir_function_signature *)SigIter.get();
				if (!Sig->is_builtin && Sig->is_defined)
				{
					SConvertStructMemberToUniform Visitor(ParseState, StructMemberMap);
					Visitor.run(&Sig->body);
				}
			}
		}
	}
	//	IRDump(Instructions, ParseState, "After FlattenUniformBufferStructures()");
}


void RemovePackedUniformBufferReferences(exec_list* Instructions, _mesa_glsl_parse_state* ParseState, TVarVarMap& UniformMap)
{
	foreach_iter(exec_list_iterator, Iter, *Instructions)
	{
		ir_instruction* Instruction = (ir_instruction*)Iter.get();
		ir_function* Function = Instruction->as_function();
		if (Function)
		{
			foreach_iter(exec_list_iterator, SigIter, *Function)
			{
				ir_function_signature* Sig = (ir_function_signature *)SigIter.get();
				if (!Sig->is_builtin && Sig->is_defined)
				{
					SFixSimpleArrayDereferencesVisitor Visitor(ParseState, &Sig->body, UniformMap);
					Visitor.run(&Sig->body);
				}
			}
		}
	}
}

/**
* Compare two uniform variables for the purpose of packing them into arrays.
*/
struct SSortUniformsPredicate
{
	_mesa_glsl_parse_state* ParseState;
	SSortUniformsPredicate(_mesa_glsl_parse_state* InParseState) : ParseState(InParseState)
	{
	}

	bool operator()(ir_variable* v1, ir_variable* v2)
	{
		const glsl_type* Type1 = v1->type;
		const glsl_type* Type2 = v2->type;

		// Sort by base type.
		const glsl_base_type BaseType1 = Type1->is_array() ? Type1->fields.array->base_type : Type1->base_type;
		const glsl_base_type BaseType2 = Type2->is_array() ? Type2->fields.array->base_type : Type2->base_type;
		if (BaseType1 != BaseType2)
		{
			static const unsigned BaseTypeOrder[GLSL_TYPE_MAX] =
			{
				0, // GLSL_TYPE_UINT,
				2, // GLSL_TYPE_INT,
				3, // GLSL_TYPE_HALF,
				4, // GLSL_TYPE_FLOAT,
				1, // GLSL_TYPE_BOOL,
				5, // GLSL_TYPE_SAMPLER,
				6, // GLSL_TYPE_STRUCT,
				7, // GLSL_TYPE_ARRAY,
				8, // GLSL_TYPE_VOID,
				9, // GLSL_TYPE_ERROR,
				10, // GLSL_TYPE_SAMPLER_STATE,
				11, // GLSL_TYPE_OUTPUTSTREAM,
				12, // GLSL_TYPE_IMAGE,
				13, // GLSL_TYPE_INPUTPATCH,
				14, // GLSL_TYPE_OUTPUTPATCH,
			};

			return BaseTypeOrder[BaseType1] < BaseTypeOrder[BaseType2];
		}

		//sort by array first
		// arrays must be aligned on a vec4 boundary, placing them first ensures this
		if (Type1->is_array() != Type2->is_array())
		{
			return int(Type1->is_array()) > int(Type2->is_array());
		}

		// Then number of vector elements.
		if (Type1->vector_elements != Type2->vector_elements)
		{
			return Type1->vector_elements > Type2->vector_elements;
		}

		// Then matrix columns.
		if (Type1->matrix_columns != Type2->matrix_columns)
		{
			return Type1->matrix_columns > Type2->matrix_columns;
		}

		// If the types match, sort on the uniform name.
		return strcmp(v1->name, v2->name) < 0;
	}
};


struct SPackedUniformsInfo
{
	struct SInfoPerArray
	{
		int NumUniforms;
		int SizeInFloats;

		TIRVarList Variables;

		SInfoPerArray() : NumUniforms(0), SizeInFloats(0) {}
	};

	typedef std::map<char, SInfoPerArray> TInfoPerArray;
	TInfoPerArray UniformArrays;

	void AddVar(ir_variable* Var, _mesa_glsl_parse_state* ParseState)
	{
		const glsl_type* type = Var->type->is_array() ? Var->type->fields.array : Var->type;
		char ArrayType = GetArrayCharFromPrecisionType(type->base_type, false);
		SInfoPerArray& Info = UniformArrays[ArrayType];

		++Info.NumUniforms;

		int Stride = (type->vector_elements > 2 || Var->type->is_array()) ? 4 : MAX2(type->vector_elements, 1u);
		int NumRows = Var->type->is_array() ? Var->type->length : 1;
		NumRows = NumRows * MAX2(type->matrix_columns, 1u);
		Info.SizeInFloats += (Stride * NumRows);
		Info.Variables.push_back(Var);
	}
};

static void FindMainAndCalculateUniformArraySizes(exec_list* Instructions, _mesa_glsl_parse_state* ParseState, ir_function_signature*& OutMain, TIRVarVector& OutUniformVariables, SPackedUniformsInfo& OutInfo)
{
	foreach_iter(exec_list_iterator, iter, *Instructions)
	{
		ir_instruction* ir = (ir_instruction*)iter.get();
		if (ir->ir_type == ir_type_variable)
		{
			ir_variable* var = (ir_variable*)ir;
			if (var->mode == ir_var_uniform)
			{
				const glsl_type* type = var->type->is_array() ? var->type->fields.array : var->type;
				if (type->IsSamplerState())
				{
					// Ignore HLSL sampler states
					continue;
				}

				if (type->is_array())
				{
					_mesa_glsl_error(ParseState, "'%s' uniform variables "
						"cannot be multi-dimensional arrays", var->name);
					goto done;
				}

				OutUniformVariables.Add(var);
				OutInfo.AddVar(var, ParseState);
			}
		}
		else if (ir->ir_type == ir_type_function && OutMain == NULL)
		{
			ir_function* func = (ir_function*)ir;
			foreach_iter(exec_list_iterator, iter, func->signatures)
			{
				ir_function_signature* sig = (ir_function_signature*)iter.get();
				if (sig->is_main)
				{
					OutMain = sig;
					break;
				}
			}
		}
	}
done:
	return;
}

static int ProcessPackedUniformArrays(exec_list* Instructions, void* ctx, _mesa_glsl_parse_state* ParseState, const TIRVarVector& UniformVariables, SPackedUniformsInfo& PUInfo, bool bFlattenStructure, bool bGroupFlattenedUBs, TVarVarMap& OutUniformMap)
{
	// First organize all uniforms by location (CB or Global) and Precision
	int UniformIndex = 0;
	std::map<std::string, std::map<char, TIRVarVector> > OrganizedVars;
	for (int NumUniforms = UniformVariables.Num(); UniformIndex < NumUniforms; ++UniformIndex)
	{
		ir_variable* var = UniformVariables[UniformIndex];
		const glsl_type* type = var->type->is_array() ? var->type->fields.array : var->type;
		const glsl_base_type array_base_type = (type->base_type == GLSL_TYPE_BOOL) ? GLSL_TYPE_UINT : type->base_type;
		if (type->is_sampler() || type->is_image())
		{
			break;
		}

		char ArrayType = GetArrayCharFromPrecisionType(array_base_type, true);
		if (!ArrayType)
		{
			_mesa_glsl_error(ParseState, "uniform '%s' has invalid type '%s'", var->name, var->type->name);
			return -1;
		}

		OrganizedVars[var->semantic ? var->semantic : ""][ArrayType].Add(var);
	}

	// Now create the list of used cb's to get their index
	std::map<std::string, int> CBIndices;
	int CBIndex = 0;
	CBIndices[""] = -1;
	for (auto Iter = ParseState->CBuffersOriginal.begin(); Iter != ParseState->CBuffersOriginal.end(); ++Iter)
	{
		auto IterFound = OrganizedVars.find(Iter->Name);
		if (IterFound != OrganizedVars.end())
		{
			CBIndices[Iter->Name] = CBIndex;
			++CBIndex;
		}
	}

	// Now actually create the packed variables
	TStringIRVarMap UniformArrayVarMap;
	std::map<std::string, std::map<char, int> > NumElementsMap;
	for (auto IterCBVarSet = OrganizedVars.begin(); IterCBVarSet != OrganizedVars.end(); ++IterCBVarSet)
	{
		std::string SourceCB = IterCBVarSet->first;
		std::string DestCB = bGroupFlattenedUBs ? SourceCB : "";
		auto& VarSet = IterCBVarSet->second;
		for (auto IterVarSet = VarSet.begin(); IterVarSet != VarSet.end(); ++IterVarSet)
		{
			ir_variable* UniformArrayVar = NULL;
			char ArrayType = IterVarSet->first;
			auto& Vars = IterVarSet->second;
			for (auto* var : Vars)
			{
				const glsl_type* type = var->type->is_array() ? var->type->fields.array : var->type;
				const glsl_base_type array_base_type = (type->base_type == GLSL_TYPE_BOOL) ? GLSL_TYPE_UINT : type->base_type;
				if (!UniformArrayVar)
				{
					std::string UniformArrayName = GetUniformArrayName(ParseState->target, type->base_type, CBIndices[DestCB]);
					auto IterFound = UniformArrayVarMap.find(UniformArrayName);
					if (IterFound == UniformArrayVarMap.end())
					{
						const glsl_type* ArrayElementType = glsl_type::get_instance(array_base_type, 4, 1);
						int NumElementsAligned = (PUInfo.UniformArrays[ArrayType].SizeInFloats + 3) / 4;
						UniformArrayVar = new(ctx) ir_variable(
							glsl_type::get_array_instance(ArrayElementType, NumElementsAligned),
							ralloc_asprintf(ParseState, "%s", UniformArrayName.c_str()),
							ir_var_uniform
							);
						UniformArrayVar->semantic = ralloc_asprintf(ParseState, "%c", ArrayType);

						Instructions->push_head(UniformArrayVar);
						if (NumElementsMap.find(DestCB) == NumElementsMap.end() || NumElementsMap[DestCB].find(ArrayType) == NumElementsMap[DestCB].end())
						{
							NumElementsMap[DestCB][ArrayType] = 0;
						}

						UniformArrayVarMap[UniformArrayName] = UniformArrayVar;
					}
					else
					{
						UniformArrayVar = IterFound->second;
					}
				}

				int& NumElements = NumElementsMap[DestCB][ArrayType];
				int Stride = (type->vector_elements > 2 || var->type->is_array()) ? 4 : MAX2(type->vector_elements, 1u);
				int NumRows = var->type->is_array() ? var->type->length : 1;
				NumRows = NumRows * MAX2(type->matrix_columns, 1u);

				glsl_packed_uniform PackedUniform;
				check(var->name);
				PackedUniform.Name = var->name;
				PackedUniform.offset = NumElements;
				PackedUniform.num_components = Stride * NumRows;
				if (!SourceCB.empty())
				{
					PackedUniform.CB_PackedSampler = SourceCB;
					ParseState->FindOffsetIntoCBufferInFloats(bFlattenStructure, var->semantic, var->name, PackedUniform.OffsetIntoCBufferInFloats, PackedUniform.SizeInFloats);
					ParseState->CBPackedArraysMap[PackedUniform.CB_PackedSampler][ArrayType].push_back(PackedUniform);
				}
				else
				{
					ParseState->GlobalPackedArraysMap[ArrayType].push_back(PackedUniform);
				}

				SUniformVarEntry Entry = { UniformArrayVar, NumElements / 4, NumElements % 4, NumRows };
				OutUniformMap[var] = Entry;

				for (int RowIndex = 0; RowIndex < NumRows; ++RowIndex)
				{
					int SrcIndex = NumElements / 4;
					int SrcComponents = NumElements % 4;
					ir_rvalue* Src = new(ctx) ir_dereference_array(
						new(ctx) ir_dereference_variable(UniformArrayVar),
						new(ctx) ir_constant(SrcIndex)
						);
					if (type->is_numeric() || type->is_boolean())
					{
						Src = new(ctx) ir_swizzle(
							Src,
							MIN2(SrcComponents + 0, 3),
							MIN2(SrcComponents + 1, 3),
							MIN2(SrcComponents + 2, 3),
							MIN2(SrcComponents + 3, 3),
							type->vector_elements
							);
					}
					if (type->is_boolean())
					{
						Src = new(ctx) ir_expression(ir_unop_u2b, Src);
					}
					ir_dereference* Dest = new(ctx) ir_dereference_variable(var);
					if (NumRows > 1 || var->type->is_array())
					{
						if (var->type->is_array() && var->type->fields.array->matrix_columns > 1)
						{
							int MatrixNum = RowIndex / var->type->fields.array->matrix_columns;
							int MatrixRow = RowIndex - (var->type->fields.array->matrix_columns * MatrixNum);
							Dest = new(ctx) ir_dereference_array(Dest, new(ctx) ir_constant(MatrixNum));
							Dest = new(ctx) ir_dereference_array(Dest, new(ctx) ir_constant(MatrixRow));
						}
						else
						{
							Dest = new(ctx) ir_dereference_array(Dest, new(ctx) ir_constant(RowIndex));
						}
					}
					var->insert_after(new(ctx) ir_assignment(Dest, Src));
					NumElements += Stride;
				}
				var->mode = ir_var_auto;

				// Update Uniform Array size to match actual usage
				NumElements = (NumElements + 3) & ~3;
				UniformArrayVar->type = glsl_type::get_array_instance(UniformArrayVar->type->fields.array, NumElements / 4);
			}
		}
	}

	return UniformIndex;
}

static int ProcessPackedSamplers(int UniformIndex, _mesa_glsl_parse_state* ParseState, const TIRVarVector& UniformVariables)
{
	int NumElements = 0;
	check(ParseState->GlobalPackedArraysMap[EArrayType_Sampler].empty());
	for (int NumUniforms = UniformVariables.Num(); UniformIndex < NumUniforms; ++UniformIndex)
	{
		ir_variable* var = UniformVariables[UniformIndex];
		const glsl_type* type = var->type->is_array() ? var->type->fields.array : var->type;

		if (!type->is_sampler() && !type->is_image())
		{
			_mesa_glsl_error(ParseState, "unexpected uniform '%s' "
				"of type '%s' when packing uniforms", var->name, var->type->name);
			return -1;
		}

		if (type->is_image())
		{
			break;
		}

		glsl_packed_uniform PackedSampler;
		check(var->name);
		PackedSampler.Name = var->name;
		PackedSampler.offset = NumElements;
		PackedSampler.num_components = var->type->is_array() ? var->type->length : 1;
		var->name = ralloc_asprintf(var, "%ss%d",
			glsl_variable_tag_from_parser_target(ParseState->target),
			NumElements);
		PackedSampler.CB_PackedSampler = var->name;
		ParseState->GlobalPackedArraysMap[EArrayType_Sampler].push_back(PackedSampler);

		NumElements += PackedSampler.num_components;
	}

	return UniformIndex;
}

static int ProcessPackedImages(int UniformIndex, _mesa_glsl_parse_state* ParseState, const TIRVarVector& UniformVariables)
{
	int NumElements = 0;
	check(ParseState->GlobalPackedArraysMap[EArrayType_Image].empty());
	for (int NumUniforms = UniformVariables.Num(); UniformIndex < NumUniforms; ++UniformIndex)
	{
		ir_variable* var = UniformVariables[UniformIndex];
		const glsl_type* type = var->type->is_array() ? var->type->fields.array : var->type;

		if (!type->is_sampler() && !type->is_image())
		{
			_mesa_glsl_error(ParseState, "unexpected uniform '%s' "
				"of type '%s' when packing uniforms", var->name, var->type->name);
			return -1;
		}

		if (type->is_sampler())
		{
			break;
		}

		glsl_packed_uniform PackedImage;
		check(var->name);
		PackedImage.Name = var->name;
		PackedImage.offset = NumElements;
		PackedImage.num_components = var->type->is_array() ? var->type->length : 1;
		ParseState->GlobalPackedArraysMap[EArrayType_Image].push_back(PackedImage);
		var->name = ralloc_asprintf(var, "%si%d",
			glsl_variable_tag_from_parser_target(ParseState->target),
			NumElements);
		
		if (ParseState->bGenerateLayoutLocations)
		{
			if (ParseState->target == compute_shader)
			{
				var->explicit_location = true;
				var->location = NumElements;
			}
			else
			{
				// easy for compute shaders, since all the bindings start at 0, harder for a set of graphics shaders
				_mesa_glsl_error(ParseState, "assigning explicit locations to UAVs/images is currently only implemented for compute shaders");
			}
		}

		NumElements += PackedImage.num_components;
	}

	return UniformIndex;
}

/**
* Pack uniforms in to typed arrays.
* @param Instructions - The IR for which to pack uniforms.
* @param ParseState - Parse state.
*/
void PackUniforms(exec_list* Instructions, _mesa_glsl_parse_state* ParseState, bool bFlattenStructure, bool bGroupFlattenedUBs, TVarVarMap& OutUniformMap)
{
	//IRDump(Instructions);
	void* ctx = ParseState;
	void* tmp_ctx = ralloc_context(NULL);
	ir_function_signature* MainSig = NULL;
	TIRVarVector UniformVariables;

	SPackedUniformsInfo PUInfo;
	FindMainAndCalculateUniformArraySizes(Instructions, ParseState, MainSig, UniformVariables, PUInfo);

	if (MainSig && UniformVariables.Num())
	{
		std::sort(UniformVariables.begin(), UniformVariables.end(), SSortUniformsPredicate(ParseState));
		int UniformIndex = ProcessPackedUniformArrays(Instructions, ctx, ParseState, UniformVariables, PUInfo, bFlattenStructure, bGroupFlattenedUBs, OutUniformMap);
		if (UniformIndex == -1)
		{
			goto done;
		}
		UniformIndex = ProcessPackedSamplers(UniformIndex, ParseState, UniformVariables);
		if (UniformIndex == -1)
		{
			goto done;
		}

		UniformIndex = ProcessPackedImages(UniformIndex, ParseState, UniformVariables);
		if (UniformIndex == -1)
		{
			goto done;
		}
	}

	ParseState->has_packed_uniforms = true;

done:
	ralloc_free(tmp_ctx);
}

struct SExpandArrayAssignment : public ir_hierarchical_visitor
{
	bool bModified;
	_mesa_glsl_parse_state* ParseState;

	std::map<const glsl_type*, std::map<std::string, int>> MemberIsArrayMap;

	SExpandArrayAssignment(_mesa_glsl_parse_state* InState) :
		ParseState(InState),
		bModified(false)
	{
	}

	ir_visitor_status DoExpandAssignment(ir_assignment* ir)
	{
		if (ir->condition)
		{
			return visit_continue;
		}

		auto* DerefVar = ir->lhs->as_dereference_variable();
		auto* DerefStruct = ir->lhs->as_dereference_record();
		if (DerefVar)
		{
			ir_variable* Var = DerefVar->variable_referenced();
			if (!Var || Var->type->array_size() <= 0)
			{
				return visit_continue;
			}

			for (int i = 0; i < Var->type->array_size(); ++i)
			{
				ir_dereference_array* NewLHS = new(ParseState) ir_dereference_array(ir->lhs->clone(ParseState, NULL), new(ParseState) ir_constant(i));
				NewLHS->type = Var->type->element_type();
				ir_dereference_array* NewRHS = new(ParseState) ir_dereference_array(ir->rhs->clone(ParseState, NULL), new(ParseState) ir_constant(i));
				NewRHS->type = Var->type->element_type();
				ir_assignment* NewCopy = new(ParseState) ir_assignment(NewLHS, NewRHS);
				ir->insert_before(NewCopy);
			}

			ir->remove();
			delete ir;
			return visit_stop;
		}
		else if (DerefStruct)
		{
			auto FoundStruct = MemberIsArrayMap.find(DerefStruct->record->type);
			if (FoundStruct == MemberIsArrayMap.end())
			{
				//glsl_struct_field* Member = nullptr;
				for (int i = 0; i < DerefStruct->record->type->length; ++i)
				{
					if (DerefStruct->record->type->fields.structure[i].type->is_array())
					{
						MemberIsArrayMap[DerefStruct->record->type][DerefStruct->record->type->fields.structure[i].name] = i;
					}
				}

				if (MemberIsArrayMap[DerefStruct->record->type].empty())
				{
					// Avoid re-caching
					MemberIsArrayMap[DerefStruct->record->type][""] = -1;
				}
				return DoExpandAssignment(ir);
			}

			auto& Members = MemberIsArrayMap[DerefStruct->record->type];
			auto FoundMember = Members.find(DerefStruct->field);
			if (FoundMember != Members.end() && FoundMember->second >= 0)
			{
				auto& Member = DerefStruct->record->type->fields.structure[FoundMember->second];
				for (int i = 0; i < Member.type->length; ++i)
				{
					ir_dereference_array* NewLHS = new(ParseState) ir_dereference_array(DerefStruct->clone(ParseState, NULL), new(ParseState) ir_constant(i));
					NewLHS->type = DerefStruct->type->element_type();
					ir_dereference_array* NewRHS = new(ParseState) ir_dereference_array(ir->rhs->clone(ParseState, NULL), new(ParseState) ir_constant(i));
					NewRHS->type = ir->rhs->type->element_type();
					ir_assignment* NewCopy = new(ParseState) ir_assignment(NewLHS, NewRHS);
					ir->insert_before(NewCopy);
				}

				ir->remove();
				delete ir;
				return visit_stop;
			}
		}

		return visit_continue;
	}

	virtual ir_visitor_status visit_leave(ir_assignment* ir) override
	{
		auto Result = DoExpandAssignment(ir);
		if (Result != visit_continue)
		{
			bModified = true;
		}

		return Result;
	}
};

// Expand any full assignments (a = b) to per element (a[0] = b[0]; a[1] = b[1]; etc) so the array can be split
bool ExpandArrayAssignments(exec_list* ir, _mesa_glsl_parse_state* State)
{
	SExpandArrayAssignment Visitor(State);
	Visitor.run(ir);

	return Visitor.bModified;
}


struct SSamplerNameVisitor : public ir_rvalue_visitor
{
	TStringToSetMap SamplerToTextureMap;
	TStringToSetMap& TextureToSamplerMap;

	SSamplerNameVisitor(TStringToSetMap& InTextureToSamplerMap) :
		TextureToSamplerMap(InTextureToSamplerMap)
	{
	}

	virtual void handle_rvalue(ir_rvalue **RValuePointer) override
	{
		ir_rvalue* RValue = *RValuePointer;
		ir_texture* TextureIR = RValue ? RValue->as_texture() : NULL;
		if (TextureIR)
		{
			if (TextureIR->SamplerState)
			{
				ir_variable* SamplerVar = TextureIR->sampler->variable_referenced();
				ir_variable* SamplerStateVar = TextureIR->SamplerState->variable_referenced();
				if (SamplerVar->mode == ir_var_uniform && SamplerStateVar->mode == ir_var_uniform)
				{
					SamplerToTextureMap[SamplerStateVar->name].insert(SamplerVar->name);
					TextureToSamplerMap[SamplerVar->name].insert(SamplerStateVar->name);

					// Remove the reference to the hlsl sampler
					ralloc_free(TextureIR->SamplerState);
					TextureIR->SamplerState = NULL;
				}
				else
				{
					int i = 0;
					++i;
				}
			}
		}
	}
};

bool ExtractSamplerStatesNameInformation(exec_list* Instructions, _mesa_glsl_parse_state* ParseState)
{
	//IRDump(Instructions);
	SSamplerNameVisitor SamplerNameVisitor(ParseState->TextureToSamplerMap);
	SamplerNameVisitor.run(Instructions);

	bool bFail = false;
	for (TStringToSetMap::iterator Iter = SamplerNameVisitor.SamplerToTextureMap.begin(); Iter != SamplerNameVisitor.SamplerToTextureMap.end(); ++Iter)
	{
		const std::string& SamplerName = Iter->first;
		const TStringSet& Textures = Iter->second;
		if (Textures.size() > 1)
		{
			_mesa_glsl_error(ParseState, "Sampler '%s' can't be used with more than one texture.\n", SamplerName.c_str());
			bFail = true;
		}
	}

	if (bFail)
	{
		return false;
	}

	return true;
}

// Removes redundant casts (A->B->A), except for the case of a truncation (float->int->float)
struct FFixRedundantCastsVisitor : public ir_rvalue_visitor
{
	FFixRedundantCastsVisitor() {}

	virtual ir_visitor_status visit_enter(ir_expression* ir)
	{
		return ir_rvalue_visitor::visit_enter(ir);
	}

	virtual ir_visitor_status visit_leave(ir_expression* ir)
	{
		auto Result = ir_rvalue_visitor::visit_leave(ir);
		return Result;
	}

	virtual void handle_rvalue(ir_rvalue** RValuePtr) override
	{
		if (!RValuePtr || !*RValuePtr)
		{
			return;
		}
		auto* Expression = (*RValuePtr)->as_expression();
		if (Expression && Expression->operation >= ir_unop_first_conversion && Expression->operation <= ir_unop_last_conversion)
		{
			auto* OperandRValue = Expression->operands[0];
			auto* OperandExpr = OperandRValue->as_expression();
			if (OperandExpr && (OperandExpr->operation >= ir_unop_first_conversion && OperandExpr->operation <= ir_unop_last_conversion))
			{
				if (Expression->type == OperandExpr->operands[0]->type)
				{
					if (Expression->type->is_float() && OperandExpr->type->is_integer())
					{
						// Skip
					}
					else
					{
						// Remove the conversion
						*RValuePtr = OperandExpr->operands[0];
					}
				}
			}
		}
	}
};

void FixRedundantCasts(exec_list* ir)
{
	FFixRedundantCastsVisitor FixRedundantCastsVisitor;
	FixRedundantCastsVisitor.run(ir);
}

// Converts matrices to arrays in order to remove non-square matrices
namespace ArraysToMatrices
{
	typedef std::map<ir_variable*, int> TArrayReplacedMap;

	// Convert matrix types to array types
	struct SConvertTypes : public ir_hierarchical_visitor
	{
		TArrayReplacedMap& NeedToFixVars;

		SConvertTypes(TArrayReplacedMap& InNeedToFixVars) : NeedToFixVars(InNeedToFixVars) {}

		virtual ir_visitor_status visit(ir_variable* IR) override
		{
			IR->type = ConvertMatrix(IR->type, IR);
			return visit_continue;
		}

		const glsl_type* ConvertMatrix(const glsl_type* Type, ir_variable* Var)
		{
			if (Type->is_array())
			{
				const auto* OriginalElementType = Type->fields.array;
				if (OriginalElementType->is_matrix())
				{
					// Arrays of matrices have to be converted into a single array of vectors
					int OriginalRows = OriginalElementType->matrix_columns;

					Type = glsl_type::get_array_instance(OriginalElementType->column_type(), OriginalRows * Type->length);

					// Need to array dereferences later
					NeedToFixVars[Var] = OriginalRows;
				}
				else
				{
					const auto* NewElementType = ConvertMatrix(OriginalElementType, Var);
					Type = glsl_type::get_array_instance(NewElementType, Type->length);
				}
			}
			//else if (Type->is_record())
			//{
			//	check(0);
			//	/*
			//	for (int i = 0; i < Type->length; ++i)
			//	{
			//	const auto* OriginalRecordType = Type->fields.structure[i].type;
			//	Type->fields.structure[i].type = ConvertMatrix(OriginalRecordType, Var);
			//	}
			//	*/
			//}
			else if (Type->is_matrix())
			{
				const auto* ColumnType = Type->column_type();
				check(Type->matrix_columns > 0);
				Type = glsl_type::get_array_instance(ColumnType, Type->matrix_columns);
			}

			return Type;
		}
	};

	// Fixes the case where matNxM A[L] is accessed by row since that requires an extra offset/multiply: A[i][r] => A[i * N + r]
	struct SFixArrays : public ir_hierarchical_visitor
	{
		TArrayReplacedMap& Entries;

		_mesa_glsl_parse_state* ParseState;
		SFixArrays(_mesa_glsl_parse_state* InParseState, TArrayReplacedMap& InEntries) : ParseState(InParseState), Entries(InEntries) {}

		virtual ir_visitor_status visit_enter(ir_dereference_array* DerefArray)
		{
			auto FoundIter = Entries.find(DerefArray->variable_referenced());
			if (FoundIter == Entries.end())
			{
				return visit_continue;
			}

			auto* ArraySubIndex = DerefArray->array->as_dereference_array();
			if (ArraySubIndex)
			{
				auto* ArrayIndexMultiplier = new(ParseState) ir_constant(FoundIter->second);
				auto* ArrayIndexMulExpression = new(ParseState)ir_expression(ir_binop_mul,ArraySubIndex->array_index,convert_component(ArrayIndexMultiplier,ArraySubIndex->array_index->type));
				DerefArray->array_index = new(ParseState) ir_expression(ir_binop_add, convert_component(ArrayIndexMulExpression, DerefArray->array_index->type), DerefArray->array_index);
				DerefArray->array = ArraySubIndex->array;
			}

			return visit_continue;
		}
	};

	// Converts a complex matrix expression into simpler ones
	// matNxM A, B, C; C = A * B + C - D * E;
	//	to:
	// T0[0] = A[0] * B[0]; (0..N-1); T1[0] = T0[0] + C[0], etc
	struct SSimplifyMatrixExpressions : public ir_rvalue_visitor
	{
		_mesa_glsl_parse_state* ParseState;

		SSimplifyMatrixExpressions(_mesa_glsl_parse_state* InParseState) :
			ParseState(InParseState)
		{
		}

		virtual void handle_rvalue(ir_rvalue** RValue) override
		{
			if (!RValue || !*RValue)
			{
				return;
			}

			ir_expression* Expression = (*RValue)->as_expression();
			if (!Expression)
			{
				return;
			}

			if (!Expression->type || !Expression->type->is_matrix())
			{
				bool bExpand = false;
				for (int i = 0; i < Expression->get_num_operands(); ++i)
				{
					bExpand |= (Expression->operands[i]->type && Expression->operands[i]->type->is_matrix());
				}

				if (!bExpand)
				{
					return;
				}
			}

			auto* NewTemporary = new(ParseState) ir_variable(Expression->type, NULL, ir_var_temporary);
			base_ir->insert_before(NewTemporary);

			for (int i = 0; i < Expression->type->matrix_columns; ++i)
			{
				auto* NewLHS = new(ParseState) ir_dereference_array(NewTemporary, new(ParseState) ir_constant(i));
				auto* NewRHS = Expression->clone(ParseState, NULL);
				for (int j = 0; j < Expression->get_num_operands(); ++j)
				{
					NewRHS->operands[j] = new(ParseState) ir_dereference_array(NewRHS->operands[j], new(ParseState) ir_constant(i));
				}
				NewRHS->type = Expression->type->column_type();
				auto* NewAssign = new(ParseState) ir_assignment(NewLHS, NewRHS);
				base_ir->insert_before(NewAssign);
			}

			*RValue = new(ParseState) ir_dereference_variable(NewTemporary);
		}
	};
}

bool ExpandMatricesIntoArrays(exec_list* Instructions, _mesa_glsl_parse_state* ParseState)
{
	ArraysToMatrices::SSimplifyMatrixExpressions ExpressionToFuncVisitor(ParseState);
	ExpressionToFuncVisitor.run(Instructions);

	ArraysToMatrices::TArrayReplacedMap NeedToFixVars;
	ArraysToMatrices::SConvertTypes ConvertVisitor(NeedToFixVars);
	ConvertVisitor.run(Instructions);
	ExpandArrayAssignments(Instructions, ParseState);
	ArraysToMatrices::SFixArrays FixDereferencesVisitor(ParseState, NeedToFixVars);
	FixDereferencesVisitor.run(Instructions);

	return true;
}
