using Stencil.TelegramBot.Domain.Projects;

namespace Stencil.TelegramBot.Application.Servers;

// Carries the origin so an aggregated cross-server listing keeps each project addressable.
public sealed record ServerProjectInfo(ProjectRecord Record, string ServerUrl);
