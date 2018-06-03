#include "Core.h"
#include "UnCore.h"
#include "TypeInfo.h"

#include "UnObject.h"		// dumping UObject in a few places

#define MAX_CLASSES		256
#define MAX_ENUMS		32

/*-----------------------------------------------------------------------------
	CTypeInfo class table
-----------------------------------------------------------------------------*/

static CClassInfo GClasses[MAX_CLASSES];
static int        GClassCount = 0;

void RegisterClasses(const CClassInfo *Table, int Count)
{
	if (Count <= 0) return;
	assert(GClassCount + Count < ARRAY_COUNT(GClasses));
	for (int i = 0; i < Count; i++)
	{
		const char* ClassName = Table[i].Name;
		bool duplicate = false;
		for (int j = 0; j < GClassCount; j++)
		{
			if (!strcmp(GClasses[j].Name, ClassName))
			{
				// Overriding class with a different typeinfo (for example, overriding UE3 class with UE4 one)
				GClasses[j] = Table[i];
				duplicate = true;
				break;
			}
		}
		if (!duplicate)
		{
			GClasses[GClassCount++] = Table[i];
		}
	}
#if DEBUG_TYPES
	appPrintf("*** Register: %d classes ***\n", Count); //!! NOTE: printing will not work correctly when "duplicate" is "true" for one or more classes
	for (int i = GClassCount - Count; i < GClassCount; i++)
		appPrintf("[%d]:%s\n", i, GClasses[i].TypeInfo()->Name);
#endif
}


// may be useful
void UnregisterClass(const char *Name, bool WholeTree)
{
	for (int i = 0; i < GClassCount; i++)
		if (!strcmp(GClasses[i].Name + 1, Name) ||
			(WholeTree && (GClasses[i].TypeInfo()->IsA(Name))))
		{
#if DEBUG_TYPES
			appPrintf("Unregister %s\n", GClasses[i].Name);
#endif
			// class was found
			if (i == GClassCount-1)
			{
				// last table entry
				GClassCount--;
				return;
			}
			memcpy(GClasses+i, GClasses+i+1, (GClassCount-i-1) * sizeof(GClasses[0]));
			GClassCount--;
			i--;
		}
}


const CTypeInfo *FindClassType(const char *Name, bool ClassType)
{
	guard(FindClassType);
#if DEBUG_TYPES
	appPrintf("--- find %s %s ... ", ClassType ? "class" : "struct", Name);
#endif
	for (int i = 0; i < GClassCount; i++)
	{
		// skip 1st char only for ClassType==true?
		if (ClassType)
		{
			if (stricmp(GClasses[i].Name + 1, Name) != 0) continue;
		}
		else
		{
			if (stricmp(GClasses[i].Name, Name) != 0) continue;
		}

		if (!GClasses[i].TypeInfo) appError("No typeinfo for class");
		const CTypeInfo *Type = GClasses[i].TypeInfo();
		if (Type->IsClass() != ClassType) continue;
#if DEBUG_TYPES
		appPrintf("ok %s\n", Type->Name);
#endif
		return Type;
	}
#if DEBUG_TYPES
	appPrintf("failed!\n");
#endif
	return NULL;
	unguardf("%s", Name);
}


bool CTypeInfo::IsA(const char *TypeName) const
{
	for (const CTypeInfo *Type = this; Type; Type = Type->Parent)
		if (!strcmp(TypeName, Type->Name + 1))
			return true;
	return false;
}


/*-----------------------------------------------------------------------------
	CTypeInfo enum table
-----------------------------------------------------------------------------*/

struct enumInfo
{
	const char       *Name;
	const enumToStr  *Values;
	int              NumValues;
};

static enumInfo RegisteredEnums[MAX_ENUMS];
static int NumEnums = 0;

void RegisterEnum(const char *EnumName, const enumToStr *Values, int Count)
{
	guard(RegisterEnum);

	assert(NumEnums < MAX_ENUMS);
	enumInfo &Info = RegisteredEnums[NumEnums++];
	Info.Name      = EnumName;
	Info.Values    = Values;
	Info.NumValues = Count;

	unguard;
}

const enumInfo *FindEnum(const char *EnumName)
{
	for (int i = 0; i < NumEnums; i++)
		if (!strcmp(RegisteredEnums[i].Name, EnumName))
			return &RegisteredEnums[i];
	return NULL;
}

const char *EnumToName(const char *EnumName, int Value)
{
	const enumInfo *Info = FindEnum(EnumName);
	if (!Info) return NULL;				// enum was not found
	for (int i = 0; i < Info->NumValues; i++)
	{
		const enumToStr &V = Info->Values[i];
		if (V.value == Value)
			return V.name;
	}
	return NULL;						// no such value
}

int NameToEnum(const char *EnumName, const char *Value)
{
	const enumInfo *Info = FindEnum(EnumName);
	if (!Info) return ENUM_UNKNOWN;		// enum was not found
	for (int i = 0; i < Info->NumValues; i++)
	{
		const enumToStr &V = Info->Values[i];
		if (!stricmp(V.name, Value))
			return V.value;
	}
	return ENUM_UNKNOWN;				// no such value
}


/*-----------------------------------------------------------------------------
	CTypeInfo property lookup
-----------------------------------------------------------------------------*/

struct PropPatch
{
	const char *ClassName;
	const char *OldName;
	const char *NewName;
};

static TArray<PropPatch> Patches;

/*static*/ void CTypeInfo::RemapProp(const char *ClassName, const char *OldName, const char *NewName)
{
	PropPatch *p = new (Patches) PropPatch;
	p->ClassName = ClassName;
	p->OldName   = OldName;
	p->NewName   = NewName;
}

const CPropInfo *CTypeInfo::FindProperty(const char *Name) const
{
	guard(CTypeInfo::FindProperty);
	int i;
	// check for remap
	for (i = 0; i < Patches.Num(); i++)
	{
		const PropPatch &p = Patches[i];
		if (!stricmp(p.ClassName, this->Name) && !stricmp(p.OldName, Name))
		{
			Name = p.NewName;
			break;
		}
	}
	// find property
	for (const CTypeInfo *Type = this; Type; Type = Type->Parent)
	{
		for (i = 0; i < Type->NumProps; i++)
			if (!(stricmp(Type->Props[i].Name, Name)))
				return Type->Props + i;
	}
	return NULL;
	unguard;
}


/*-----------------------------------------------------------------------------
	CTypeInfo dump functionality
-----------------------------------------------------------------------------*/

struct CPropDump
{
	FStaticString<32>	Name;
	FStaticString<32>	Value;
	TArray<CPropDump>	Nested;				// Value should be "" when Nested[] is not empty
	bool				IsArrayItem;

	CPropDump()
	: IsArrayItem(false)
	{}

	void PrintName(const char *fmt, ...)
	{
		va_list	argptr;
		va_start(argptr, fmt);
		PrintTo(Name, fmt, argptr);
		va_end(argptr);
	}

	void PrintValue(const char *fmt, ...)
	{
		va_list	argptr;
		va_start(argptr, fmt);
		PrintTo(Value, fmt, argptr);
		va_end(argptr);
	}

private:
	void PrintTo(FString& Dst, const char *fmt, va_list argptr)
	{
		char buffer[1024];
		vsnprintf(ARRAY_ARG(buffer), fmt, argptr);
		Dst += buffer;
	}
};


static void CollectProps(const CTypeInfo *Type, void *Data, CPropDump &Dump)
{
	for (/* empty */; Type; Type = Type->Parent)
	{
		if (!Type->NumProps) continue;

		for (int PropIndex = 0; PropIndex < Type->NumProps; PropIndex++)
		{
			const CPropInfo *Prop = Type->Props + PropIndex;
			if (!Prop->TypeName)
			{
//				appPrintf("  %3d: (dummy) %s\n", PropIndex, Prop->Name);
				continue;
			}
			CPropDump *PD = new (Dump.Nested) CPropDump;

			// name

#if DUMP_SHOW_PROP_INDEX
			PD->PrintName("(%d)", PropIndex);
#endif
#if DUMP_SHOW_PROP_TYPE
			PD->PrintName("%s ", (Prop->TypeName[0] != '#') ? Prop->TypeName : Prop->TypeName+1);	// skip enum marker
#endif
			PD->PrintName("%s", Prop->Name);

			// value

			byte *value = (byte*)Data + Prop->Offset;
			int PropCount = Prop->Count;

			bool IsArray = (PropCount > 1) || (PropCount == -1);
			if (PropCount == -1)
			{
				// TArray<> value
				FArray *Arr = (FArray*)value;
				value     = (byte*)Arr->GetData();
				PropCount = Arr->Num();
			}

			// find structure type
			const CTypeInfo *StrucType = FindStructType(Prop->TypeName);
			bool IsStruc = (StrucType != NULL);

			// formatting of property start
			if (IsArray)
			{
				PD->PrintName("[%d]", PropCount);
				if (!PropCount)
				{
					PD->PrintValue("{}");
					continue;
				}
			}

			// dump item(s)
			for (int ArrayIndex = 0; ArrayIndex < PropCount; ArrayIndex++)
			{
				CPropDump *PD2 = PD;
				if (IsArray)
				{
					// create nested CPropDump
					PD2 = new (PD->Nested) CPropDump;
					PD2->PrintName("%s[%d]", Prop->Name, ArrayIndex);
					PD2->IsArrayItem = true;
				}

				// note: ArrayIndex is used inside PROP macro

#define PROP(type)	( ((type*)value)[ArrayIndex] )

#define IS(name) 	strcmp(Prop->TypeName, #name) == 0

#define PROCESS(type, format, value)	\
					if (IS(type))		\
					{					\
						PD2->PrintValue(format, value); \
					}

				PROCESS(byte,     "%d", PROP(byte));
				PROCESS(int,      "%d", PROP(int));
				PROCESS(bool,     "%s", PROP(bool) ? "true" : "false");
				PROCESS(float,    "%g", PROP(float));
#if 1
				if (IS(UObject*))
				{
					UObject *obj = PROP(UObject*);
					if (obj)
					{
						char ObjName[256];
						obj->GetFullName(ARRAY_ARG(ObjName));
						PD2->PrintValue("%s'%s'", obj->GetClassName(), ObjName);
					}
					else
						PD2->PrintValue("None");
				}
#else
				PROCESS(UObject*, "%s", PROP(UObject*) ? PROP(UObject*)->Name : "Null");
#endif
				PROCESS(FName,    "%s", *PROP(FName));
				if (Prop->TypeName[0] == '#')
				{
					// enum value
					const char *v = EnumToName(Prop->TypeName+1, *value);		// skip enum marker
					PD2->PrintValue("%s (%d)", v ? v : "<unknown>", *value);
				}
				if (IsStruc)
				{
					// this is a structure type
					CollectProps(StrucType, value + ArrayIndex * StrucType->SizeOf, *PD2);
				}
			} // ArrayIndex loop
		} // PropIndex loop
	} // Type->Parent loop
}


static void PrintIndent(FArchive& Ar, int Value)
{
	for (int i = 0; i < Value; i++)
		Ar.Printf("    ");
}

static void PrintProps(const CPropDump &Dump, FArchive& Ar, int Indent)
{
	PrintIndent(Ar, Indent);

	int NumNestedProps = Dump.Nested.Num();
	if (NumNestedProps)
	{
		// complex property
		if (!Dump.Name.IsEmpty()) Ar.Printf("%s =", *Dump.Name);	// root CPropDump will not have a name

		bool IsSimple = true;
		int TotalLen = 0;
		int i;

		// check whether we can display all nested properties in a single line or not
		for (i = 0; i < NumNestedProps; i++)
		{
			const CPropDump &Prop = Dump.Nested[i];
			if (Prop.Nested.Num())
			{
				IsSimple = false;
				break;
			}
			TotalLen += Prop.Value.Len() + 2;
			if (!Prop.IsArrayItem)
				TotalLen += Prop.Name.Len();
			if (TotalLen >= 80)
			{
				IsSimple = false;
				break;
			}
		}

		if (IsSimple)
		{
			// single-line value display
			Ar.Printf(" { ");
			for (i = 0; i < NumNestedProps; i++)
			{
				if (i) Ar.Printf(", ");
				const CPropDump &Prop = Dump.Nested[i];
				if (Prop.IsArrayItem)
					Ar.Printf("%s", *Prop.Value);
				else
					Ar.Printf("%s=%s", *Prop.Name, *Prop.Value);
			}
			Ar.Printf(" }\n");
		}
		else
		{
			// complex value display
			Ar.Printf("\n");
			if (Indent > 0)
			{
				PrintIndent(Ar, Indent);
				Ar.Printf("{\n");
			}

			for (i = 0; i < NumNestedProps; i++)
				PrintProps(Dump.Nested[i], Ar, Indent+1);

			if (Indent > 0)
			{
				PrintIndent(Ar, Indent);
				Ar.Printf("}\n");
			}
		}
	}
	else
	{
		// single property
		if (!Dump.Name.IsEmpty()) Ar.Printf("%s = %s\n", *Dump.Name, *Dump.Value);
	}
}


void CTypeInfo::DumpProps(void *Data) const
{
	guard(CTypeInfo::DumpProps);
	CPropDump Dump;
	CollectProps(this, Data, Dump);

	FPrintfArchive Ar;
	PrintProps(Dump, Ar, 0);

	unguard;
}
