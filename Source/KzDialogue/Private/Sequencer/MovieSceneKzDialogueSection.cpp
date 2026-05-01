// Copyright 2026 kirzo

#include "Sequencer/MovieSceneKzDialogueSection.h"

#include "Channels/MovieSceneChannelProxy.h"

UMovieSceneKzDialogueSection::UMovieSceneKzDialogueSection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSupportsInfiniteRange = false;
	BlendType = EMovieSceneBlendType::Absolute;
	EvalOptions.EnableAndSetCompletionMode(EMovieSceneCompletionMode::ProjectDefault);
}

EMovieSceneChannelProxyType UMovieSceneKzDialogueSection::CacheChannelProxy()
{
	// No animatable channels — we only carry a LineId payload, so an empty proxy suffices.
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>();
	return EMovieSceneChannelProxyType::Static;
}