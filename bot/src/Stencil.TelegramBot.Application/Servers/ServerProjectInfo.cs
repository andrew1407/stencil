using Stencil.TelegramBot.Domain.Projects;

namespace Stencil.TelegramBot.Application.Servers;

/// <summary>
/// A project record paired with the normalised origin of the server it came from, so an
/// aggregated cross-server listing keeps each project addressable. Mirrors the way the
/// browser/<c>pystencil</c> tag every project in a multi-server list with its connection.
/// </summary>
public sealed record ServerProjectInfo(ProjectRecord Record, string ServerUrl);
