#pragma once


#include "parser.hpp"
#include "parser_plug_in.hpp"
#include "api.h"
#include "file_stream.hpp"


struct udtParserPlugInQuakeToUDT : udtBaseParserPlugIn
{
public:
	udtParserPlugInQuakeToUDT();
	~udtParserPlugInQuakeToUDT();

	void SetOutputStream(udtStream* output);

	void InitAllocators(u32 demoCount) override;
	u32  GetElementSize() const override { return 0; }

	void StartDemoAnalysis() override;
	void FinishDemoAnalysis() override;
	void ProcessGamestateMessage(const udtGamestateCallbackArg& arg, udtBaseParser& parser) override;
	void ProcessSnapshotMessage(const udtSnapshotCallbackArg& arg, udtBaseParser& parser) override;
	void ProcessCommandMessage(const udtCommandCallbackArg& arg, udtBaseParser& parser) override;

private:
	UDT_NO_COPY_SEMANTICS(udtParserPlugInQuakeToUDT);

private:
	void WriteSnapshot(udtBaseParser& parser);

	struct udtdClientEntity
	{
		idEntityStateBase EntityState;
		bool Valid;
	};

	struct udtdSnapshot
	{
		udtdClientEntity Entities[MAX_GENTITIES];
		s32 ServerTime;
	};

	struct udtdData
	{
		udtdSnapshot Snapshots[2];
		s32 SnapshotReadIndex;
		s32 LastSnapshotTimeMs;
	};

	udtStream* _outputFile;
	udtdData* _data;
	bool _firstSnapshot;
};